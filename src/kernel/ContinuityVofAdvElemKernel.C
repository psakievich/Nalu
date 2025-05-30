/*------------------------------------------------------------------------*/
/*  Copyright 2014 Sandia Corporation.                                    */
/*  This software is released under the license detailed                  */
/*  in the file, LICENSE, which is located in the top-level Nalu          */
/*  directory structure                                                   */
/*------------------------------------------------------------------------*/

#include "kernel/ContinuityVofAdvElemKernel.h"
#include "AlgTraits.h"
#include "master_element/MasterElement.h"
#include "TimeIntegrator.h"
#include "SolutionOptions.h"

// template and scratch space
#include "BuildTemplates.h"
#include "ScratchViews.h"

// stk_mesh/base/fem
#include <stk_mesh/base/Entity.hpp>
#include <stk_mesh/base/MetaData.hpp>
#include <stk_mesh/base/BulkData.hpp>
#include <stk_mesh/base/Field.hpp>

namespace sierra {
namespace nalu {

template<typename AlgTraits>
ContinuityVofAdvElemKernel<AlgTraits>::ContinuityVofAdvElemKernel(
  const stk::mesh::BulkData& bulkData,
  const SolutionOptions& solnOpts,
  ElemDataRequests& dataPreReqs)
  : Kernel(),
    meshMotion_(solnOpts.does_mesh_move()),
    shiftMdot_(solnOpts.cvfemShiftMdot_),
    shiftPoisson_(solnOpts.get_shifted_grad_op("pressure")),
    reducedSensitivities_(solnOpts.cvfemReducedSensPoisson_),
    n_(solnOpts.localVofN_),
    m_(solnOpts.localVofM_),
    c_(solnOpts.localVofC_),
    lrscv_(sierra::nalu::MasterElementRepo::get_surface_master_element(AlgTraits::topo_)->adjacentNodes())
{
  // Save of required fields
  const stk::mesh::MetaData& metaData = bulkData.mesh_meta_data();
  std::string velocity_name = meshMotion_ ? "velocity_rtm" : "velocity";

  velocityRTM_ = metaData.get_field<double>(stk::topology::NODE_RANK, velocity_name);
  Gpdx_ = metaData.get_field<double>(stk::topology::NODE_RANK, "dpdx");
  pressure_ = metaData.get_field<double>(stk::topology::NODE_RANK, "pressure");
  ScalarFieldType *density = metaData.get_field<double>(stk::topology::NODE_RANK, "density");
  densityNp1_ = &(density->field_of_state(stk::mesh::StateNP1));
  interfaceCurvature_ = metaData.get_field<double>(stk::topology::NODE_RANK, "interface_curvature");
  surfaceTension_ = metaData.get_field<double>(stk::topology::NODE_RANK, "surface_tension");
  vof_ = metaData.get_field<double>(stk::topology::NODE_RANK, "volume_of_fluid");
  coordinates_ = metaData.get_field<double>(stk::topology::NODE_RANK, solnOpts.get_coordinates_name());

  for (int i = 0; i < AlgTraits::nDim_; ++i)
    gravity_(i) = solnOpts.gravity_[i];

  if(solnOpts.buoyancyPressureStab_)
    buoyancyWeight_ = 1.0;
  else
    buoyancyWeight_ = 0.0;

  MasterElement *meSCS = sierra::nalu::MasterElementRepo::get_surface_master_element(AlgTraits::topo_);
  dataPreReqs.add_cvfem_surface_me(meSCS);

  // fields and data
  dataPreReqs.add_coordinates_field(*coordinates_, AlgTraits::nDim_, CURRENT_COORDINATES);
  dataPreReqs.add_gathered_nodal_field(*velocityRTM_, AlgTraits::nDim_);
  dataPreReqs.add_gathered_nodal_field(*densityNp1_, 1);
  dataPreReqs.add_gathered_nodal_field(*pressure_, 1);
  dataPreReqs.add_gathered_nodal_field(*interfaceCurvature_, 1);
  dataPreReqs.add_gathered_nodal_field(*surfaceTension_, 1);
  dataPreReqs.add_gathered_nodal_field(*vof_, 1);
  dataPreReqs.add_gathered_nodal_field(*Gpdx_, AlgTraits::nDim_);
  dataPreReqs.add_master_element_call(SCS_AREAV, CURRENT_COORDINATES);

  // manage dndx
  if ( !shiftPoisson_ || !reducedSensitivities_ )
    dataPreReqs.add_master_element_call(SCS_GRAD_OP, CURRENT_COORDINATES);
  if ( shiftPoisson_ || reducedSensitivities_ )
    dataPreReqs.add_master_element_call(SCS_SHIFTED_GRAD_OP, CURRENT_COORDINATES);

  if ( shiftMdot_ )
    get_scs_shape_fn_data<AlgTraits>([&](double* ptr){meSCS->shifted_shape_fcn(ptr);}, v_shape_function_);
  else
    get_scs_shape_fn_data<AlgTraits>([&](double* ptr){meSCS->shape_fcn(ptr);}, v_shape_function_);
}

template<typename AlgTraits>
ContinuityVofAdvElemKernel<AlgTraits>::~ContinuityVofAdvElemKernel()
{}

template<typename AlgTraits>
void
ContinuityVofAdvElemKernel<AlgTraits>::setup(const TimeIntegrator& timeIntegrator)
{
  const double dt = timeIntegrator.get_time_step();
  const double gamma1 = timeIntegrator.get_gamma1();
  projTimeScale_ = dt/gamma1;
}

template<typename AlgTraits>
void
ContinuityVofAdvElemKernel<AlgTraits>::execute(
  SharedMemView<DoubleType **>& lhs,
  SharedMemView<DoubleType *>& rhs,
  ScratchViews<DoubleType>& scratchViews)
{
  // Work arrays (fixed size)
  NALU_ALIGNED DoubleType w_uIp [AlgTraits::nDim_];
  NALU_ALIGNED DoubleType w_GpdxIp [AlgTraits::nDim_];
  NALU_ALIGNED DoubleType w_dpdxIp  [AlgTraits::nDim_];

  SharedMemView<DoubleType*>& v_densityNp1 = scratchViews.get_scratch_view_1D(*densityNp1_);
  SharedMemView<DoubleType*>& v_pressure = scratchViews.get_scratch_view_1D(*pressure_);

  SharedMemView<DoubleType*>& v_kappa = scratchViews.get_scratch_view_1D(*interfaceCurvature_);
  SharedMemView<DoubleType*>& v_sigma = scratchViews.get_scratch_view_1D(*surfaceTension_);
  SharedMemView<DoubleType*>& v_vof = scratchViews.get_scratch_view_1D(*vof_);

  SharedMemView<DoubleType**>& v_velocity = scratchViews.get_scratch_view_2D(*velocityRTM_);
  SharedMemView<DoubleType**>& v_Gpdx = scratchViews.get_scratch_view_2D(*Gpdx_);

  SharedMemView<DoubleType**>& v_scs_areav = scratchViews.get_me_views(CURRENT_COORDINATES).scs_areav;

  SharedMemView<DoubleType***>& v_dndx = shiftPoisson_ ?
    scratchViews.get_me_views(CURRENT_COORDINATES).dndx_shifted : scratchViews.get_me_views(CURRENT_COORDINATES).dndx;
  SharedMemView<DoubleType***>& v_dndx_lhs = (shiftPoisson_ || reducedSensitivities_)?
    scratchViews.get_me_views(CURRENT_COORDINATES).dndx_shifted : scratchViews.get_me_views(CURRENT_COORDINATES).dndx;

  for (int ip = 0; ip < AlgTraits::numScsIp_; ++ip) {
    const int il = lrscv_[2*ip];
    const int ir = lrscv_[2*ip+1];

    // need density at ip first for proper LHS scaling
    DoubleType rhoIp = 0.0;
    DoubleType dvofdaIp = 0.0;
    DoubleType sigmaKappaIp = 0.0;
    DoubleType vofIp = 0.0;
    for (int ic = 0; ic < AlgTraits::nodesPerElement_; ++ic) {
      rhoIp += v_shape_function_(ip, ic)*v_densityNp1(ic);
      vofIp += v_shape_function_(ip, ic)*v_vof(ic);
    }
    
    for (int j = 0; j < AlgTraits::nDim_; ++j) {
      w_uIp[j] = 0.0;
      w_GpdxIp[j] = 0.0;
      w_dpdxIp[j] = 0.0;
    }
    
    for (int ic = 0; ic < AlgTraits::nodesPerElement_; ++ic) {
      const DoubleType r = v_shape_function_(ip, ic);
      sigmaKappaIp += r*v_sigma(ic)*v_kappa(ic);
      const DoubleType pressureIc = v_pressure(ic);
      const DoubleType vofIc = v_vof(ic);

      DoubleType lhsfac = 0.0;
      for (int j = 0; j < AlgTraits::nDim_; ++j) {
        w_GpdxIp[j] += r*v_Gpdx(ic, j);
        w_uIp[j] += r*v_velocity(ic, j);
        w_dpdxIp[j]  += v_dndx(ip,ic,j)*pressureIc;
        dvofdaIp += v_dndx(ip,ic,j)*vofIc*v_scs_areav(ip,j);
        lhsfac += -v_dndx_lhs(ip, ic, j)*v_scs_areav(ip,j);
      }

      lhs(il,ic) += lhsfac/rhoIp;
      lhs(ir,ic) -= lhsfac/rhoIp;
    }

    // correct for localized approach
    sigmaKappaIp *= c_*stk::math::pow(vofIp,n_)*stk::math::pow(1.0-vofIp,m_);

    // assemble flow rate
    DoubleType vdot = projTimeScale_*sigmaKappaIp*dvofdaIp/rhoIp;
    for (int j = 0; j < AlgTraits::nDim_; ++j) {      
      // balanced force approach
      vdot += (w_uIp[j] - projTimeScale_*((w_dpdxIp[j]-buoyancyWeight_*rhoIp*gravity_(j))/rhoIp - w_GpdxIp[j]))*v_scs_areav(ip,j);
    }

    // residuals
    rhs(il) -= vdot/projTimeScale_;
    rhs(ir) += vdot/projTimeScale_;
  }
}

INSTANTIATE_KERNEL(ContinuityVofAdvElemKernel);

}  // nalu
}  // sierra
