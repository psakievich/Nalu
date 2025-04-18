/*------------------------------------------------------------------------*/
/*  Copyright 2014 Sandia Corporation.                                    */
/*  This software is released under the license detailed                  */
/*  in the file, LICENSE, which is located in the top-level Nalu          */
/*  directory structure                                                   */
/*------------------------------------------------------------------------*/


// nalu
#include <TurbViscWaleAlgorithm.h>
#include <Algorithm.h>
#include <FieldTypeDef.h>
#include <Realm.h>

// stk_mesh/base/fem
#include <stk_mesh/base/BulkData.hpp>
#include <stk_mesh/base/GetBuckets.hpp>
#include <stk_mesh/base/MetaData.hpp>
#include <stk_mesh/base/Part.hpp>
#include <stk_mesh/base/Field.hpp>

namespace sierra{
namespace nalu{

//==========================================================================
// Class Definition
//==========================================================================
// TurbViscWaleAlgorithm - compute tvisc for Ksgs model
//==========================================================================
//--------------------------------------------------------------------------
//-------- constructor -----------------------------------------------------
//--------------------------------------------------------------------------
TurbViscWaleAlgorithm::TurbViscWaleAlgorithm(
  Realm &realm,
  stk::mesh::Part *part)
  : Algorithm(realm, part),
    dudx_(NULL),
    density_(NULL),
    tvisc_(NULL),
    dualNodalVolume_(NULL),
    Cw_(realm.get_turb_model_constant(TM_Cw)),
    kappa_(realm.get_turb_model_constant(TM_kappa))
{

  stk::mesh::MetaData & meta_data = realm_.meta_data();

  dudx_ = meta_data.get_field<double>(stk::topology::NODE_RANK, "dudx");
  density_ = meta_data.get_field<double>(stk::topology::NODE_RANK, "density");
  tvisc_ = meta_data.get_field<double>(stk::topology::NODE_RANK, "turbulent_viscosity");
  dualNodalVolume_ = meta_data.get_field<double>(stk::topology::NODE_RANK, "dual_nodal_volume");
  // need NDTW...
}

//--------------------------------------------------------------------------
//-------- execute ---------------------------------------------------------
//--------------------------------------------------------------------------
void
TurbViscWaleAlgorithm::execute()
{

  stk::mesh::MetaData & meta_data = realm_.meta_data();

  const int nDim = meta_data.spatial_dimension();
  const double invNdim = 1.0/double(nDim);

  // save some factors
  const double threeHalves = 3.0/2.0;
  const double fiveHalves = 5.0/2.0;
  const double fiveFourths = 5.0/4.0;
  const double small = 1.0e-8;
  const double kd[3][3] = { {1.0, 0.0, 0.0}, 
                            {0.0, 1.0, 0.0}, 
                            {0.0, 0.0, 1.0} }; 
  
  // define some common selectors
  stk::mesh::Selector s_all_nodes
    = (meta_data.locally_owned_part() | meta_data.globally_shared_part())
    &stk::mesh::selectField(*tvisc_);

  stk::mesh::BucketVector const& node_buckets =
    realm_.get_buckets( stk::topology::NODE_RANK, s_all_nodes );
  for ( stk::mesh::BucketVector::const_iterator ib = node_buckets.begin();
        ib != node_buckets.end() ; ++ib ) {
    stk::mesh::Bucket & b = **ib ;
    const stk::mesh::Bucket::size_type length   = b.size();

    const double *density = stk::mesh::field_data(*density_, b);
    const double *dualNodalVolume = stk::mesh::field_data(*dualNodalVolume_, b);
    double *tvisc = stk::mesh::field_data(*tvisc_, b);

    for ( stk::mesh::Bucket::size_type k = 0 ; k < length ; ++k ) {

      const double *dudx = stk::mesh::field_data(*dudx_, b[k] );
      
      double gkkSq = 0.0;
      for ( int m = 0; m < nDim; ++m ) {
        const int offSetM = nDim*m;
        for ( int n = 0; n < nDim; ++n ) {
          const int offSetN = nDim*n;
          gkkSq += dudx[offSetM+n]*dudx[offSetN+m];
        }
      }

      double SijSq = 0.0;
      double SijdSq = 0.0;
      for ( int i = 0; i < nDim; ++i ) {
        const int offSetI = nDim*i;
        for ( int j = 0; j < nDim; ++j ) {
          const int offSetJ = nDim*j;
          const double Sij = 0.5*(dudx[offSetI+j] + dudx[offSetJ+i]);
          double gijSq = 0.0;
          double gjiSq = 0.0;
          for ( int l = 0; l < nDim; ++l ) {
            const int offSetL = nDim*l;
            gijSq += dudx[offSetI+l]*dudx[offSetL+j];
            gjiSq += dudx[offSetJ+l]*dudx[offSetL+i];
          }
          const double Sijd = 0.5*(gijSq + gjiSq) - kd[i][j]/3.0*gkkSq;
          SijSq += Sij*Sij;
          SijdSq += Sijd*Sijd;
        }
      }

      const double filter = std::pow(dualNodalVolume[k], invNdim);
      const double Ls = Cw_*filter;
      const double numer = std::pow(SijdSq, threeHalves) + small*small;
      const double demom = std::pow(SijSq, fiveHalves)+std::pow(SijdSq, fiveFourths) + small;
      tvisc[k] = density[k]*Ls*Ls*numer/demom;
    }
  }
}

} // namespace nalu
} // namespace Sierra
