/*------------------------------------------------------------------------*/
/*  Copyright 2014 Sandia Corporation.                                    */
/*  This software is released under the license detailed                  */
/*  in the file, LICENSE, which is located in the top-level Nalu          */
/*  directory structure                                                   */
/*------------------------------------------------------------------------*/


#include <user_functions/VariableDensityNonIsoContinuitySrcNodeSuppAlg.h>
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
// VariableDensityNonIsoContinuitySrcNodeSuppAlg - base class for algorithm
//==========================================================================
//--------------------------------------------------------------------------
//-------- constructor -----------------------------------------------------
//--------------------------------------------------------------------------
VariableDensityNonIsoContinuitySrcNodeSuppAlg::VariableDensityNonIsoContinuitySrcNodeSuppAlg(
  Realm &realm)
  : SupplementalAlgorithm(realm),
    coordinates_(NULL),
    dualNodalVolume_(NULL),
    unot_(1.0),
    vnot_(1.0),
    wnot_(1.0),
    hnot_(1.0),
    a_(20.0),
    ah_(10.0),
    Pref_(100.0),
    MW_(30.0),
    R_(10.0),
    Tref_(300.0),
    Cp_(0.01),
    Pr_(0.8),
    pi_(acos(-1.0)),
    projTimeScale_(1.0)
{
  // save off fields
  stk::mesh::MetaData & meta_data = realm_.meta_data();
  coordinates_ = meta_data.get_field<double>(stk::topology::NODE_RANK, realm_.get_coordinates_name());
  dualNodalVolume_ = meta_data.get_field<double>(stk::topology::NODE_RANK, "dual_nodal_volume");
}

//--------------------------------------------------------------------------
//-------- setup -----------------------------------------------------------
//--------------------------------------------------------------------------
void
VariableDensityNonIsoContinuitySrcNodeSuppAlg::setup()
{
  projTimeScale_ = realm_.get_time_step()/realm_.get_gamma1();
}

//--------------------------------------------------------------------------
//-------- node_execute ----------------------------------------------------
//--------------------------------------------------------------------------
void
VariableDensityNonIsoContinuitySrcNodeSuppAlg::node_execute(
  double */*lhs*/,
  double *rhs,
  stk::mesh::Entity node)
{
  // deal with lumped mass matrix
  const double *coords = stk::mesh::field_data(*coordinates_, node);
  const double dualVolume = *stk::mesh::field_data(*dualNodalVolume_, node );

  const double x = coords[0];
  const double y = coords[1];
  const double z = coords[2];

  const double src = -Pref_ * MW_ / R_ * pow(hnot_ * cos(ah_ * pi_ * x) * cos(ah_ * pi_ * y) * cos(ah_ * pi_ * z) / Cp_ + Tref_, -0.2e1) * unot_ * cos(a_ * pi_ * x) * sin(a_ * pi_ * y) * sin(a_ * pi_ * z) * hnot_ * sin(ah_ * pi_ * x) * ah_ * pi_ * cos(ah_ * pi_ * y) * cos(ah_ * pi_ * z) / Cp_ + Pref_ * MW_ / R_ / (hnot_ * cos(ah_ * pi_ * x) * cos(ah_ * pi_ * y) * cos(ah_ * pi_ * z) / Cp_ + Tref_) * unot_ * sin(a_ * pi_ * x) * a_ * pi_ * sin(a_ * pi_ * y) * sin(a_ * pi_ * z) + Pref_ * MW_ / R_ * pow(hnot_ * cos(ah_ * pi_ * x) * cos(ah_ * pi_ * y) * cos(ah_ * pi_ * z) / Cp_ + Tref_, -0.2e1) * vnot_ * sin(a_ * pi_ * x) * cos(a_ * pi_ * y) * sin(a_ * pi_ * z) * hnot_ * cos(ah_ * pi_ * x) * sin(ah_ * pi_ * y) * ah_ * pi_ * cos(ah_ * pi_ * z) / Cp_ - Pref_ * MW_ / R_ / (hnot_ * cos(ah_ * pi_ * x) * cos(ah_ * pi_ * y) * cos(ah_ * pi_ * z) / Cp_ + Tref_) * vnot_ * sin(a_ * pi_ * x) * sin(a_ * pi_ * y) * a_ * pi_ * sin(a_ * pi_ * z) - Pref_ * MW_ / R_ * pow(hnot_ * cos(ah_ * pi_ * x) * cos(ah_ * pi_ * y) * cos(ah_ * pi_ * z) / Cp_ + Tref_, -0.2e1) * wnot_ * sin(a_ * pi_ * x) * sin(a_ * pi_ * y) * cos(a_ * pi_ * z) * hnot_ * cos(ah_ * pi_ * x) * cos(ah_ * pi_ * y) * sin(ah_ * pi_ * z) * ah_ * pi_ / Cp_ + Pref_ * MW_ / R_ / (hnot_ * cos(ah_ * pi_ * x) * cos(ah_ * pi_ * y) * cos(ah_ * pi_ * z) / Cp_ + Tref_) * wnot_ * sin(a_ * pi_ * x) * sin(a_ * pi_ * y) * sin(a_ * pi_ * z) * a_ * pi_;
 
  rhs[0] += src*dualVolume/projTimeScale_;
}

} // namespace nalu
} // namespace Sierra
