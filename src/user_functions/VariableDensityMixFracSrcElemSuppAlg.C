/*------------------------------------------------------------------------*/
/*  Copyright 2014 Sandia Corporation.                                    */
/*  This software is released under the license detailed                  */
/*  in the file, LICENSE, which is located in the top-level Nalu          */
/*  directory structure                                                   */
/*------------------------------------------------------------------------*/


#include <user_functions/VariableDensityMixFracSrcElemSuppAlg.h>
#include <SupplementalAlgorithm.h>
#include <FieldTypeDef.h>
#include <Realm.h>
#include <master_element/MasterElement.h>

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
// VariableDensityMixFracSrcElemSuppAlg - base class for algorithm
//==========================================================================
//--------------------------------------------------------------------------
//-------- constructor -----------------------------------------------------
//--------------------------------------------------------------------------
VariableDensityMixFracSrcElemSuppAlg::VariableDensityMixFracSrcElemSuppAlg(
  Realm &realm)
  : SupplementalAlgorithm(realm),
    bulkData_(&realm.bulk_data()),
    coordinates_(NULL),
    nDim_(realm_.spatialDimension_),
    rhoP_(0.1),
    rhoS_(1.0),
    unot_(1.0),
    vnot_(1.0),
    wnot_(1.0),
    znot_(1.0),
    pnot_(1.0),
    visc_(0.001),
    a_(20.0),
    amf_(10.0),
    Sc_(0.8),
    pi_(acos(-1.0)),
    useShifted_(false)
{
  // save off fields
  stk::mesh::MetaData & meta_data = realm_.meta_data();
  coordinates_ = meta_data.get_field<double>(stk::topology::NODE_RANK, realm_.get_coordinates_name());
 
  // scratch vecs
  scvCoords_.resize(nDim_);
}

//--------------------------------------------------------------------------
//-------- elem_resize -----------------------------------------------------
//--------------------------------------------------------------------------
void
VariableDensityMixFracSrcElemSuppAlg::elem_resize(
  MasterElement */*meSCS*/,
  MasterElement *meSCV)
{
  const int nodesPerElement = meSCV->nodesPerElement_;
  const int numScvIp = meSCV->numIntPoints_;
  ws_shape_function_.resize(numScvIp*nodesPerElement);
  ws_coordinates_.resize(nDim_*nodesPerElement);
  ws_scv_volume_.resize(numScvIp);

  // compute shape function
  if ( useShifted_ )
    meSCV->shifted_shape_fcn(&ws_shape_function_[0]);
  else
    meSCV->shape_fcn(&ws_shape_function_[0]);
}

//--------------------------------------------------------------------------
//-------- setup -----------------------------------------------------------
//--------------------------------------------------------------------------
void
VariableDensityMixFracSrcElemSuppAlg::setup()
{
  // nothing
}

//--------------------------------------------------------------------------
//-------- elem_execute ----------------------------------------------------
//--------------------------------------------------------------------------
void
VariableDensityMixFracSrcElemSuppAlg::elem_execute(
  double */*lhs*/,
  double *rhs,
  stk::mesh::Entity element,
  MasterElement */*meSCS*/,
  MasterElement *meSCV)
{
  // pointer to ME methods
  const int *ipNodeMap = meSCV->ipNodeMap();
  const int nodesPerElement = meSCV->nodesPerElement_;
  const int numScvIp = meSCV->numIntPoints_;
  
  // gather
  stk::mesh::Entity const *  node_rels = bulkData_->begin_nodes(element);
  int num_nodes = bulkData_->num_nodes(element);
  
  // sanity check on num nodes
  STK_ThrowAssert( num_nodes == nodesPerElement );
  
  for ( int ni = 0; ni < num_nodes; ++ni ) {
    stk::mesh::Entity node = node_rels[ni];
    // pointers to real data
    const double * coords = stk::mesh::field_data(*coordinates_, node );
    // gather vectors
    const int niNdim = ni*nDim_;
    for ( int j=0; j < nDim_; ++j ) {
      ws_coordinates_[niNdim+j] = coords[j];
    }
  }
  
  // compute geometry
  double scv_error = 0.0;
  meSCV->determinant(1, &ws_coordinates_[0], &ws_scv_volume_[0], &scv_error);

  for ( int ip = 0; ip < numScvIp; ++ip ) {
    
    // nearest node to ip
    const int nearestNode = ipNodeMap[ip];
    
    // zero out
    for ( int j =0; j < nDim_; ++j )
      scvCoords_[j] = 0.0;
       
    const int offSet = ip*nodesPerElement;
    for ( int ic = 0; ic < nodesPerElement; ++ic ) {
      const double r = ws_shape_function_[offSet+ic];
      for ( int j = 0; j < nDim_; ++j )
        scvCoords_[j] += r*ws_coordinates_[ic*nDim_+j];
    }

    const double x = scvCoords_[0];
    const double y = scvCoords_[1];
    const double z = scvCoords_[2];
    
    const double src = 0.10e1 * pow(znot_ * cos(amf_ * pi_ * x) * cos(amf_ * pi_ * y) * cos(amf_ * pi_ * z) / rhoP_ + (0.1e1 - znot_ * cos(amf_ * pi_ * x) * cos(amf_ * pi_ * y) * cos(amf_ * pi_ * z)) / rhoS_, -0.2e1) * unot_ * cos(a_ * pi_ * x) * sin(a_ * pi_ * y) * sin(a_ * pi_ * z) * znot_ * cos(amf_ * pi_ * x) * cos(amf_ * pi_ * y) * cos(amf_ * pi_ * z) * (-znot_ * sin(amf_ * pi_ * x) * amf_ * pi_ * cos(amf_ * pi_ * y) * cos(amf_ * pi_ * z) / rhoP_ + znot_ * sin(amf_ * pi_ * x) * amf_ * pi_ * cos(amf_ * pi_ * y) * cos(amf_ * pi_ * z) / rhoS_) + 0.10e1 / (znot_ * cos(amf_ * pi_ * x) * cos(amf_ * pi_ * y) * cos(amf_ * pi_ * z) / rhoP_ + (0.1e1 - znot_ * cos(amf_ * pi_ * x) * cos(amf_ * pi_ * y) * cos(amf_ * pi_ * z)) / rhoS_) * unot_ * sin(a_ * pi_ * x) * a_ * pi_ * sin(a_ * pi_ * y) * sin(a_ * pi_ * z) * znot_ * cos(amf_ * pi_ * x) * cos(amf_ * pi_ * y) * cos(amf_ * pi_ * z) + 0.10e1 / (znot_ * cos(amf_ * pi_ * x) * cos(amf_ * pi_ * y) * cos(amf_ * pi_ * z) / rhoP_ + (0.1e1 - znot_ * cos(amf_ * pi_ * x) * cos(amf_ * pi_ * y) * cos(amf_ * pi_ * z)) / rhoS_) * unot_ * cos(a_ * pi_ * x) * sin(a_ * pi_ * y) * sin(a_ * pi_ * z) * znot_ * sin(amf_ * pi_ * x) * amf_ * pi_ * cos(amf_ * pi_ * y) * cos(amf_ * pi_ * z) - 0.10e1 * pow(znot_ * cos(amf_ * pi_ * x) * cos(amf_ * pi_ * y) * cos(amf_ * pi_ * z) / rhoP_ + (0.1e1 - znot_ * cos(amf_ * pi_ * x) * cos(amf_ * pi_ * y) * cos(amf_ * pi_ * z)) / rhoS_, -0.2e1) * vnot_ * sin(a_ * pi_ * x) * cos(a_ * pi_ * y) * sin(a_ * pi_ * z) * znot_ * cos(amf_ * pi_ * x) * cos(amf_ * pi_ * y) * cos(amf_ * pi_ * z) * (-znot_ * cos(amf_ * pi_ * x) * sin(amf_ * pi_ * y) * amf_ * pi_ * cos(amf_ * pi_ * z) / rhoP_ + znot_ * cos(amf_ * pi_ * x) * sin(amf_ * pi_ * y) * amf_ * pi_ * cos(amf_ * pi_ * z) / rhoS_) - 0.10e1 / (znot_ * cos(amf_ * pi_ * x) * cos(amf_ * pi_ * y) * cos(amf_ * pi_ * z) / rhoP_ + (0.1e1 - znot_ * cos(amf_ * pi_ * x) * cos(amf_ * pi_ * y) * cos(amf_ * pi_ * z)) / rhoS_) * vnot_ * sin(a_ * pi_ * x) * sin(a_ * pi_ * y) * a_ * pi_ * sin(a_ * pi_ * z) * znot_ * cos(amf_ * pi_ * x) * cos(amf_ * pi_ * y) * cos(amf_ * pi_ * z) - 0.10e1 / (znot_ * cos(amf_ * pi_ * x) * cos(amf_ * pi_ * y) * cos(amf_ * pi_ * z) / rhoP_ + (0.1e1 - znot_ * cos(amf_ * pi_ * x) * cos(amf_ * pi_ * y) * cos(amf_ * pi_ * z)) / rhoS_) * vnot_ * sin(a_ * pi_ * x) * cos(a_ * pi_ * y) * sin(a_ * pi_ * z) * znot_ * cos(amf_ * pi_ * x) * sin(amf_ * pi_ * y) * amf_ * pi_ * cos(amf_ * pi_ * z) + 0.10e1 * pow(znot_ * cos(amf_ * pi_ * x) * cos(amf_ * pi_ * y) * cos(amf_ * pi_ * z) / rhoP_ + (0.1e1 - znot_ * cos(amf_ * pi_ * x) * cos(amf_ * pi_ * y) * cos(amf_ * pi_ * z)) / rhoS_, -0.2e1) * wnot_ * sin(a_ * pi_ * x) * sin(a_ * pi_ * y) * cos(a_ * pi_ * z) * znot_ * cos(amf_ * pi_ * x) * cos(amf_ * pi_ * y) * cos(amf_ * pi_ * z) * (-znot_ * cos(amf_ * pi_ * x) * cos(amf_ * pi_ * y) * sin(amf_ * pi_ * z) * amf_ * pi_ / rhoP_ + znot_ * cos(amf_ * pi_ * x) * cos(amf_ * pi_ * y) * sin(amf_ * pi_ * z) * amf_ * pi_ / rhoS_) + 0.10e1 / (znot_ * cos(amf_ * pi_ * x) * cos(amf_ * pi_ * y) * cos(amf_ * pi_ * z) / rhoP_ + (0.1e1 - znot_ * cos(amf_ * pi_ * x) * cos(amf_ * pi_ * y) * cos(amf_ * pi_ * z)) / rhoS_) * wnot_ * sin(a_ * pi_ * x) * sin(a_ * pi_ * y) * sin(a_ * pi_ * z) * a_ * pi_ * znot_ * cos(amf_ * pi_ * x) * cos(amf_ * pi_ * y) * cos(amf_ * pi_ * z) + 0.10e1 / (znot_ * cos(amf_ * pi_ * x) * cos(amf_ * pi_ * y) * cos(amf_ * pi_ * z) / rhoP_ + (0.1e1 - znot_ * cos(amf_ * pi_ * x) * cos(amf_ * pi_ * y) * cos(amf_ * pi_ * z)) / rhoS_) * wnot_ * sin(a_ * pi_ * x) * sin(a_ * pi_ * y) * cos(a_ * pi_ * z) * znot_ * cos(amf_ * pi_ * x) * cos(amf_ * pi_ * y) * sin(amf_ * pi_ * z) * amf_ * pi_ + 0.3e1 * visc_ / Sc_ * znot_ * cos(amf_ * pi_ * x) * amf_ * amf_ * pi_ * pi_ * cos(amf_ * pi_ * y) * cos(amf_ * pi_ * z);

    rhs[nearestNode] += src*ws_scv_volume_[ip];      
  }
}

} // namespace nalu
} // namespace Sierra
