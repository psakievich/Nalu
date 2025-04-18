/*------------------------------------------------------------------------*/
/*  Copyright 2014 Sandia Corporation.                                    */
/*  This software is released under the license detailed                  */
/*  in the file, LICENSE, which is located in the top-level Nalu          */
/*  directory structure                                                   */
/*------------------------------------------------------------------------*/

#include "kernel/TurbKineticEnergyKsgsSrcElemKernel.h"
#include "AlgTraits.h"
#include "Enums.h"
#include "SolutionOptions.h"
#include "master_element/MasterElement.h"

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
TurbKineticEnergyKsgsSrcElemKernel<AlgTraits>::TurbKineticEnergyKsgsSrcElemKernel(
  const stk::mesh::BulkData& bulkData,
  const SolutionOptions& solnOpts,
  ElemDataRequests& dataPreReqs,
  double lrksgsfac)
  : Kernel(),
    lrksgsfac_(lrksgsfac),
    tkeProdLimitRatio_(solnOpts.get_turb_model_constant(TM_tkeProdLimitRatio)),
    ipNodeMap_(sierra::nalu::MasterElementRepo::get_volume_master_element(AlgTraits::topo_)->ipNodeMap())
{
  // save off fields
  const stk::mesh::MetaData& metaData = bulkData.mesh_meta_data();
  coordinates_ = metaData.get_field<double>(stk::topology::NODE_RANK, solnOpts.get_coordinates_name());
  tkeNp1_ = metaData.get_field<double>(stk::topology::NODE_RANK, "turbulent_ke");
  densityNp1_ = metaData.get_field<double>(stk::topology::NODE_RANK, "density");
  tvisc_ = metaData.get_field<double>(stk::topology::NODE_RANK, "turbulent_viscosity");
  dualNodalVolume_ = metaData.get_field<double>(stk::topology::NODE_RANK, "dual_nodal_volume");
  cEps_ = metaData.get_field<double>(stk::topology::NODE_RANK, "c_epsilon");
  Gju_ = metaData.get_field<double>(stk::topology::NODE_RANK, "dudx");

  // low-Re form
  visc_ = metaData.get_field<double>(stk::topology::NODE_RANK, "viscosity");
  // assign required variables that may not be registered to an arbitrary field
  dsqrtkSq_ = metaData.get_field<double>(stk::topology::NODE_RANK, "viscosity");
  if (lrksgsfac > 0.0 ) {
    dsqrtkSq_ = metaData.get_field<double>(stk::topology::NODE_RANK, "dsqrtk_dx_sq");
  }

  MasterElement *meSCV = sierra::nalu::MasterElementRepo::get_volume_master_element(AlgTraits::topo_);

  // add master elements
  dataPreReqs.add_cvfem_volume_me(meSCV);

  // required fields
  dataPreReqs.add_coordinates_field(*coordinates_, AlgTraits::nDim_, CURRENT_COORDINATES);
  dataPreReqs.add_gathered_nodal_field(*tkeNp1_, 1);
  dataPreReqs.add_gathered_nodal_field(*densityNp1_, 1);
  dataPreReqs.add_gathered_nodal_field(*tvisc_, 1);
  dataPreReqs.add_gathered_nodal_field(*dualNodalVolume_, 1);
  dataPreReqs.add_gathered_nodal_field(*cEps_, 1);
  dataPreReqs.add_gathered_nodal_field(*Gju_, AlgTraits::nDim_, AlgTraits::nDim_);
  dataPreReqs.add_gathered_nodal_field(*visc_, 1);
  dataPreReqs.add_gathered_nodal_field(*dsqrtkSq_, 1);
  dataPreReqs.add_master_element_call(SCV_VOLUME, CURRENT_COORDINATES);
}

template<typename AlgTraits>
TurbKineticEnergyKsgsSrcElemKernel<AlgTraits>::~TurbKineticEnergyKsgsSrcElemKernel()
{}

template<typename AlgTraits>
void
TurbKineticEnergyKsgsSrcElemKernel<AlgTraits>::execute(
  SharedMemView<DoubleType **>&lhs,
  SharedMemView<DoubleType *>&rhs,
  ScratchViews<DoubleType>& scratchViews)
{
  SharedMemView<DoubleType*>& v_tkeNp1 = scratchViews.get_scratch_view_1D(
    *tkeNp1_);
  SharedMemView<DoubleType*>& v_densityNp1 = scratchViews.get_scratch_view_1D(
    *densityNp1_);
  SharedMemView<DoubleType*>& v_tvisc = scratchViews.get_scratch_view_1D(
    *tvisc_);
  SharedMemView<DoubleType*>& v_dualNodalVolume = scratchViews.get_scratch_view_1D(
    *dualNodalVolume_);
  SharedMemView<DoubleType*>& v_cEps = scratchViews.get_scratch_view_1D(
    *cEps_);
  SharedMemView<DoubleType***>& v_Gju = scratchViews.get_scratch_view_3D(*Gju_);
  SharedMemView<DoubleType*>& v_scv_volume = scratchViews.get_me_views(CURRENT_COORDINATES).scv_volume;
  SharedMemView<DoubleType*>& v_visc = scratchViews.get_scratch_view_1D(
    *visc_);
  SharedMemView<DoubleType*>& v_dsqrtkSq = scratchViews.get_scratch_view_1D(
    *dsqrtkSq_);

  for (int ip=0; ip < AlgTraits::numScvIp_; ++ip) {
    const int nearestNode = ipNodeMap_[ip];

    DoubleType Pk = 0.0;
    for ( int i = 0; i < AlgTraits::nDim_; ++i ) {
      for ( int j = 0; j < AlgTraits::nDim_; ++j ) {
        Pk += v_Gju(nearestNode,i,j)*(v_Gju(nearestNode,i,j) + v_Gju(nearestNode,j,i));
      }
    }
    Pk *= v_tvisc(nearestNode);

    // tke factor
    const DoubleType tke = v_tkeNp1(nearestNode);
    const DoubleType tkeFac = (AlgTraits::nDim_ == 2) ?
      v_cEps(nearestNode)*v_densityNp1(nearestNode)*stk::math::sqrt(tke/v_dualNodalVolume(nearestNode))
      : v_cEps(nearestNode)*v_densityNp1(nearestNode)*stk::math::sqrt(tke)/stk::math::cbrt(v_dualNodalVolume(nearestNode));

    // dissipation and production; limited
    DoubleType Dk = tkeFac * tke + lrksgsfac_*2.0*v_visc(nearestNode)*v_dsqrtkSq(nearestNode);
    Pk = stk::math::min(Pk, tkeProdLimitRatio_*Dk);
    
    // lhs assembly, all lumped
    const DoubleType scvol = v_scv_volume(ip);
    rhs(nearestNode) += (Pk - Dk)*scvol;
    lhs(nearestNode,nearestNode) += 1.5*tkeFac*scvol;
  }
}

INSTANTIATE_KERNEL(TurbKineticEnergyKsgsSrcElemKernel);

}  // nalu
}  // sierra
