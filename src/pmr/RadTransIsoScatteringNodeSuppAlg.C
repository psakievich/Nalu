/*------------------------------------------------------------------------*/
/*  Copyright 2014 Sandia Corporation.                                    */
/*  This software is released under the license detailed                  */
/*  in the file, LICENSE, which is located in the top-level Nalu          */
/*  directory structure                                                   */
/*------------------------------------------------------------------------*/


#include <pmr/RadTransIsoScatteringNodeSuppAlg.h>
#include <pmr/RadiativeTransportEquationSystem.h>
#include <SupplementalAlgorithm.h>
#include <FieldTypeDef.h>
#include <Realm.h>
#include <TimeIntegrator.h>

// stk_mesh/base/fem
#include <stk_mesh/base/Entity.hpp>
#include <stk_mesh/base/MetaData.hpp>
#include <stk_mesh/base/BulkData.hpp>
#include <stk_mesh/base/Field.hpp>

namespace sierra{
namespace nalu{

//==========================================================================
// Class Definition
//==========================================================================
// RadTransIsoScatteringNodeSuppAlg - base class for algorithm
//==========================================================================
//--------------------------------------------------------------------------
//-------- constructor -----------------------------------------------------
//--------------------------------------------------------------------------
RadTransIsoScatteringNodeSuppAlg::RadTransIsoScatteringNodeSuppAlg(
  Realm &realm,
  RadiativeTransportEquationSystem *radEqSystem)
  : SupplementalAlgorithm(realm),
    radEqSystem_(radEqSystem),
    scalarFlux_(NULL),
    scattering_(NULL),
    dualNodalVolume_(NULL),
    invPi_(1.0/std::acos(-1.0))
{
  // save off fields
  stk::mesh::MetaData & meta_data = realm_.meta_data();
  scalarFlux_ = meta_data.get_field<double>(stk::topology::NODE_RANK, "scalar_flux");
  scattering_ = meta_data.get_field<double>(stk::topology::NODE_RANK, "scattering_coefficient");
  dualNodalVolume_ = meta_data.get_field<double>(stk::topology::NODE_RANK, "dual_nodal_volume");
}

//--------------------------------------------------------------------------
//-------- node_execute ----------------------------------------------------
//--------------------------------------------------------------------------
void
RadTransIsoScatteringNodeSuppAlg::node_execute(
  double *lhs,
  double *rhs,
  stk::mesh::Entity node)
{
  // (-beta*G/4/pi)*dVol = 0.0
  const double scalarFlux  = *stk::mesh::field_data(*scalarFlux_, node);
  const double beta = *stk::mesh::field_data(*scattering_, node);
  const double dualVolume = *stk::mesh::field_data(*dualNodalVolume_, node);
  rhs[0] += beta*scalarFlux/4.0*invPi_*dualVolume;
  lhs[0] += 0.0;
}

} // namespace nalu
} // namespace Sierra
