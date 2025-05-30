/*------------------------------------------------------------------------*/
/*  Copyright 2014 Sandia Corporation.                                    */
/*  This software is released under the license detailed                  */
/*  in the file, LICENSE, which is located in the top-level Nalu          */
/*  directory structure                                                   */
/*------------------------------------------------------------------------*/


#include <TemperaturePmrSrcNodeSuppAlg.h>
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
// TemperaturePmrSrcNodeSuppAlg -  rho*cp*d/dt(T) = -divQr
//--------------------------------------------------------------------------
//-------- constructor -----------------------------------------------------
//--------------------------------------------------------------------------
TemperaturePmrSrcNodeSuppAlg::TemperaturePmrSrcNodeSuppAlg(
  Realm &realm)
  : SupplementalAlgorithm(realm),
    divRadFlux_(NULL),
    divRadFluxLin_(NULL),
    dualNodalVolume_(NULL)
{
  // save off fields
  stk::mesh::MetaData & meta_data = realm_.meta_data();
  divRadFlux_ = meta_data.get_field<double>(stk::topology::NODE_RANK, "div_radiative_heat_flux");
  divRadFluxLin_ = meta_data.get_field<double>(stk::topology::NODE_RANK, "div_radiative_heat_flux_lin");
  dualNodalVolume_ = meta_data.get_field<double>(stk::topology::NODE_RANK, "dual_nodal_volume");
}

//--------------------------------------------------------------------------
//-------- node_execute ----------------------------------------------------
//--------------------------------------------------------------------------
void
TemperaturePmrSrcNodeSuppAlg::node_execute(
  double *lhs,
  double *rhs,
  stk::mesh::Entity node)
{
  // implicit based on linearization value (may or may not be zero) 
  const double divQ = *stk::mesh::field_data(*divRadFlux_, node );
  const double divQLin = *stk::mesh::field_data(*divRadFluxLin_, node );
  const double dualVolume = *stk::mesh::field_data(*dualNodalVolume_, node );

  rhs[0] -= divQ*dualVolume;
  lhs[0] += divQLin*dualVolume;
}

} // namespace nalu
} // namespace Sierra
