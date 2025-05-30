/*------------------------------------------------------------------------*/
/*  Copyright 2014 Sandia Corporation.                                    */
/*  This software is released under the license detailed                  */
/*  in the file, LICENSE, which is located in the top-level Nalu          */
/*  directory structure                                                   */
/*------------------------------------------------------------------------*/


// nalu
#include <AssembleMomentumElemWallFunctionProjectedSolverAlgorithm.h>
#include <SolverAlgorithm.h>
#include <EquationSystem.h>
#include <LinearSystem.h>
#include <PointInfo.h>
#include <FieldTypeDef.h>
#include <Realm.h>
#include <master_element/MasterElement.h>

// stk_mesh/base/fem
#include <stk_mesh/base/BulkData.hpp>
#include <stk_mesh/base/Field.hpp>
#include <stk_mesh/base/FieldParallel.hpp>
#include <stk_mesh/base/GetBuckets.hpp>
#include <stk_mesh/base/GetEntities.hpp>
#include <stk_mesh/base/MetaData.hpp>
#include <stk_mesh/base/Part.hpp>

// basic c++
#include <cmath>

namespace sierra{
namespace nalu{

//==========================================================================
// Class Definition
//==========================================================================
// AssembleMomentumElemWallFunctionProjectedSolverAlgorithm - elem wall function
//==========================================================================
//--------------------------------------------------------------------------
//-------- constructor -----------------------------------------------------
//--------------------------------------------------------------------------
AssembleMomentumElemWallFunctionProjectedSolverAlgorithm::AssembleMomentumElemWallFunctionProjectedSolverAlgorithm(
  Realm &realm,
  stk::mesh::Part *part,
  EquationSystem *eqSystem,
  const bool &useShifted,
  std::map<std::string, std::vector<std::vector<PointInfo *> > > &pointInfoMap,
  stk::mesh::Ghosting *wallFunctionGhosting)
  : SolverAlgorithm(realm, part, eqSystem),
    useShifted_(useShifted),
    pointInfoMap_(pointInfoMap),
    wallFunctionGhosting_(wallFunctionGhosting),
    yplusCrit_(11.63),
    elog_(9.8),
    kappa_(realm.get_turb_model_constant(TM_kappa))
{
  // save off fields
  stk::mesh::MetaData & meta_data = realm_.meta_data();
  velocity_ = meta_data.get_field<double>(stk::topology::NODE_RANK, "velocity");
  bcVelocity_ = meta_data.get_field<double>(stk::topology::NODE_RANK, "wall_velocity_bc");
  density_ = meta_data.get_field<double>(stk::topology::NODE_RANK, "density");
  viscosity_ = meta_data.get_field<double>(stk::topology::NODE_RANK, "viscosity");
  exposedAreaVec_ = meta_data.get_field<double>(meta_data.side_rank(), "exposed_area_vector");
  wallFrictionVelocityBip_ = meta_data.get_field<double>(meta_data.side_rank(), "wall_friction_velocity_bip");
  wallNormalDistanceBip_ = meta_data.get_field<double>(meta_data.side_rank(), "wall_normal_distance_bip");

  // what do we need ghosted for this alg to work?
  ghostFieldVec_.push_back(&(velocity_->field_of_state(stk::mesh::StateNP1)));
}

//--------------------------------------------------------------------------
//-------- initialize_connectivity -----------------------------------------
//--------------------------------------------------------------------------
void
AssembleMomentumElemWallFunctionProjectedSolverAlgorithm::initialize_connectivity()
{
  // iterate parts to match pointInfoMap_ construction
  for ( size_t k = 0; k < partVec_.size(); ++k ) {
    stk::mesh::PartVector partVec;
    partVec.push_back(partVec_[k]);
    eqSystem_->linsys_->buildFaceToNodeGraph(partVec);
  }
}

//--------------------------------------------------------------------------
//-------- execute ---------------------------------------------------------
//--------------------------------------------------------------------------
void
AssembleMomentumElemWallFunctionProjectedSolverAlgorithm::execute()
{
  stk::mesh::BulkData & bulk_data = realm_.bulk_data();
  stk::mesh::MetaData & meta_data = realm_.meta_data();

  const int nDim = meta_data.spatial_dimension();

  // space for LHS/RHS; nodesPerFace*nDim*nodesPerFace*nDim and nodesPerFace*nDim
  std::vector<double> lhs;
  std::vector<double> rhs;
  std::vector<int> scratchIds;
  std::vector<double> scratchVals;
  std::vector<stk::mesh::Entity> connected_nodes;

  // bip values
  std::vector<double> uProjected(nDim);
  std::vector<double> uBcBip(nDim);
  std::vector<double> unitNormal(nDim);
  std::vector<double> uiTanVec(nDim);
  std::vector<double> uiBcTanVec(nDim);

  // pointers to fixed values
  double *p_uProjected = &uProjected[0];
  double *p_uBcBip = &uBcBip[0];
  double *p_unitNormal= &unitNormal[0];
  double *p_uiTanVec = &uiTanVec[0];
  double *p_uiBcTanVec = &uiBcTanVec[0];

  // nodal fields to gather
  std::vector<double> ws_bcVelocity;
  std::vector<double> ws_density;
  std::vector<double> ws_viscosity;

  // master element
  std::vector<double> ws_face_shape_function;

  // deal with state
  VectorFieldType &velocityNp1 = velocity_->field_of_state(stk::mesh::StateNP1);
  ScalarFieldType &densityNp1 = density_->field_of_state(stk::mesh::StateNP1);

  // parallel communicate ghosted entities
  if ( nullptr != wallFunctionGhosting_ )
    stk::mesh::communicate_field_data(*(wallFunctionGhosting_), ghostFieldVec_);

  // iterate over parts (requires part-based local counter over locally owned faces)
  for ( size_t pv = 0; pv < partVec_.size(); ++pv ) {
        
    // extract name 
    const std::string partName = partVec_[pv]->name();

    // set counter for this particular part
    size_t pointInfoVecCounter = 0;
    
    // define selector (per part)
    stk::mesh::Selector s_locally_owned 
      = meta_data.locally_owned_part() &stk::mesh::Selector(*partVec_[pv]);

    // extract local vector for this part
    std::vector<std::vector<PointInfo *> > *pointInfoVec = nullptr;
    std::map<std::string, std::vector<std::vector<PointInfo *> > >::iterator itf =
      pointInfoMap_.find(partName);
    if ( itf == pointInfoMap_.end() ) {
      // will need to throw
      NaluEnv::self().naluOutputP0() << "cannot find pointInfoMap_ with part name: " << partName << std::endl;
      throw std::runtime_error("AssembleMomentumElemWallFunctionProjectedSolverAlgorithm::issue");
    }
    else {
      pointInfoVec = &((*itf)).second;
    }
    
    stk::mesh::BucketVector const& face_buckets =
      realm_.get_buckets( meta_data.side_rank(), s_locally_owned );
    for ( stk::mesh::BucketVector::const_iterator ib = face_buckets.begin();
          ib != face_buckets.end() ; ++ib ) {
      stk::mesh::Bucket & b = **ib ;
      
      // face master element
      MasterElement *meFC = sierra::nalu::MasterElementRepo::get_surface_master_element(b.topology());
      const int nodesPerFace = meFC->nodesPerElement_;
      const int numScsBip = meFC->numIntPoints_;
      
      // mapping from ip to nodes for this ordinal; face perspective (use with face_node_relations)
      const int *faceIpNodeMap = meFC->ipNodeMap();
      
      // resize some things; matrix related
      const int lhsSize = nodesPerFace*nDim*nodesPerFace*nDim;
      const int rhsSize = nodesPerFace*nDim;
      lhs.resize(lhsSize);
      rhs.resize(rhsSize);
      scratchIds.resize(rhsSize);
      scratchVals.resize(rhsSize);
      connected_nodes.resize(nodesPerFace);
      
      // algorithm related; element
      ws_bcVelocity.resize(nodesPerFace*nDim);
      ws_density.resize(nodesPerFace);
      ws_viscosity.resize(nodesPerFace);
      ws_face_shape_function.resize(numScsBip*nodesPerFace);
      
      // pointers
      double *p_lhs = &lhs[0];
      double *p_rhs = &rhs[0];
      double *p_bcVelocity = &ws_bcVelocity[0];
      double *p_density = &ws_density[0];
      double *p_viscosity = &ws_viscosity[0];
      double *p_face_shape_function = &ws_face_shape_function[0];
      
      // shape functions
      if ( useShifted_ )
        meFC->shifted_shape_fcn(&p_face_shape_function[0]);
      else
        meFC->shape_fcn(&p_face_shape_function[0]);
      
      const stk::mesh::Bucket::size_type length   = b.size();
      
      for ( stk::mesh::Bucket::size_type k = 0 ; k < length ; ++k ) {
        
        // zero lhs/rhs
        for ( int p = 0; p < lhsSize; ++p )
          p_lhs[p] = 0.0;
        for ( int p = 0; p < rhsSize; ++p )
          p_rhs[p] = 0.0;
        
        // get face
        stk::mesh::Entity face = b[k];
        
        //======================================
        // gather nodal data off of face
        //======================================
        stk::mesh::Entity const * face_node_rels = bulk_data.begin_nodes(face);
        for ( int ni = 0; ni < nodesPerFace; ++ni ) {
          stk::mesh::Entity node = face_node_rels[ni];
          connected_nodes[ni] = node;
          
          // gather scalars
          p_density[ni]    = *stk::mesh::field_data(densityNp1, node);
          p_viscosity[ni] = *stk::mesh::field_data(*viscosity_, node);
          
          // gather vectors
          double * uBc = stk::mesh::field_data(*bcVelocity_, node);
          const int niNdim = ni*nDim;
          for ( int j=0; j < nDim; ++j ) {
            p_bcVelocity[niNdim+j] = uBc[j];
          }
        }
        
        // pointer to face data
        const double * areaVec = stk::mesh::field_data(*exposedAreaVec_, face);
        const double *wallNormalDistanceBip = stk::mesh::field_data(*wallNormalDistanceBip_, face);
        const double *wallFrictionVelocityBip = stk::mesh::field_data(*wallFrictionVelocityBip_, face);
        
        // extract the vector of PointInfo for this face
        std::vector<PointInfo *> &faceInfoVec = (*pointInfoVec)[pointInfoVecCounter++];
        
        for ( int ip = 0; ip < numScsBip; ++ip ) {
          
          // offsets
          const int ipNdim = ip*nDim;
          const int ipNpf = ip*nodesPerFace;
          
          const int localFaceNode = faceIpNodeMap[ip];
          
          // extract point info for this ip - must matches the construction of the pInfo vector
          PointInfo *pInfo = faceInfoVec[ip];
          stk::mesh::Entity owningElement = pInfo->owningElement_;
          
          // get master element type for this contactInfo
          MasterElement *meSCS  = pInfo->meSCS_;
          const int nodesPerElement = meSCS->nodesPerElement_;
          std::vector <double > elemNodalVelocity(nodesPerElement*nDim);
          std::vector <double > shpfc(nodesPerElement);
          
          // gather element data
          stk::mesh::Entity const* elem_node_rels = bulk_data.begin_nodes(owningElement);
          const int num_elem_nodes = bulk_data.num_nodes(owningElement);
          for ( int ni = 0; ni < num_elem_nodes; ++ni ) {
            stk::mesh::Entity node = elem_node_rels[ni];
            // gather velocity (conforms to interpolatePoint)
            const double *uNp1 = stk::mesh::field_data(velocityNp1, node );
            for ( int j = 0; j < nDim; ++j ) {
              elemNodalVelocity[j*nodesPerElement+ni] = uNp1[j];
            }
          }
          
          // interpolate to elemental point location
          meSCS->interpolatePoint(
           nDim,
           &(pInfo->isoParCoords_[0]),
           &elemNodalVelocity[0],
           &uProjected[0]);
        
          // zero out vector quantities; squeeze in aMag
          double aMag = 0.0;
          for ( int j = 0; j < nDim; ++j ) {
            p_uBcBip[j] = 0.0;
            const double axj = areaVec[ipNdim+j];
            aMag += axj*axj;
          }
          aMag = std::sqrt(aMag);
          
          // interpolate to bip
          double rhoBip = 0.0;
          double muBip = 0.0;
          for ( int ic = 0; ic < nodesPerFace; ++ic ) {
            const double r = p_face_shape_function[ipNpf+ic];
            rhoBip += r*p_density[ic];
            muBip += r*p_viscosity[ic];
            const int icNdim = ic*nDim;
            for ( int j = 0; j < nDim; ++j ) {
              p_uBcBip[j] += r*p_bcVelocity[icNdim+j];
            }
          }
          
          // form unit normal
          for ( int j = 0; j < nDim; ++j ) {
            p_unitNormal[j] = areaVec[ipNdim+j]/aMag;
          }
          
          // extract bip data
          const double yp = wallNormalDistanceBip[ip];
          const double utau = wallFrictionVelocityBip[ip];
          
          // determine yplus
          const double yplus = rhoBip*yp*utau/muBip;
          
          double lambda = muBip/yp*aMag;
          if ( yplus > yplusCrit_)
            lambda = rhoBip*kappa_*utau/std::log(elog_*yplus)*aMag;

          // correct for ODE-based approach, tauW = rho*utau*utau (given by ODE solve)
          const double odeFac = pInfo->odeFac_;
          const double om_odeFac = 1.0 - odeFac;
          lambda = lambda*om_odeFac + odeFac*rhoBip*utau*utau*aMag;

          // determine tangential velocity
          double uTangential = 0.0;
          for ( int i = 0; i < nDim; ++i ) {                
            double uiTan = 0.0;
            double uiBcTan = 0.0;
            for ( int j = 0; j < nDim; ++j ) {
              const double ninj = p_unitNormal[i]*p_unitNormal[j];
              if ( i==j ) {
                const double om_nini = 1.0 - ninj;
                uiTan += om_nini*p_uProjected[j];
                uiBcTan += om_nini*p_uBcBip[j];
              }
              else {
                uiTan -= ninj*p_uProjected[j];
                uiBcTan -= ninj*p_uBcBip[j];
              }
            }
            uTangential += (uiTan-uiBcTan)*(uiTan-uiBcTan);
            // save off for later matrix assembly
            p_uiTanVec[i] = uiTan;
            p_uiBcTanVec[i] = uiBcTan;
          }
          uTangential = std::sqrt(uTangential);
          
          // start the rhs assembly (lhs neglected) - account for possible ode
          const double normalizeFac = 1.0/(om_odeFac + odeFac*uTangential);
          for ( int i = 0; i < nDim; ++i ) {            
            int indexR = localFaceNode*nDim + i;
            p_rhs[indexR] -= lambda*(p_uiTanVec[i]-p_uiBcTanVec[i])*normalizeFac;
          }
        }
        
        apply_coeff(connected_nodes, scratchIds, scratchVals, rhs, lhs, __FILE__);
        
      }
    }
  }
}

} // namespace nalu
} // namespace Sierra
