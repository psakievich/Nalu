/*------------------------------------------------------------------------*/
/*  Copyright 2014 Sandia Corporation.                                    */
/*  This software is released under the license detailed                  */
/*  in the file, LICENSE, which is located in the top-level Nalu          */
/*  directory structure                                                   */
/*------------------------------------------------------------------------*/


#include <user_functions/RayleighTaylorMixFracAuxFunction.h>
#include <algorithm>

// basic c++
#include <cmath>
#include <vector>
#include <stdexcept>

namespace sierra{
namespace nalu{

RayleighTaylorMixFracAuxFunction::RayleighTaylorMixFracAuxFunction(
    const std::vector<double> &theParams) :
  AuxFunction(0,1),
  aX_(0.1),
  tX_(1.0),
  yTr_(1.0),
  dTr_(0.20),
  surf_(1.0),
  pi_(acos(-1.0))
{
  // extract the params - if they are supplied (optional)
  if ( theParams.size() > 0 )
    aX_ = theParams[0];
  if ( theParams.size() > 1 )
    tX_ = theParams[1];
  if ( theParams.size() > 2 )
    yTr_ = theParams[2];
  if ( theParams.size() > 3 )
    dTr_ = theParams[3];
  if ( theParams.size() > 4 )
    surf_ = theParams[4];
}

void
RayleighTaylorMixFracAuxFunction::do_evaluate(
  const double *coords,
  const double /*time*/,
  const unsigned spatialDimension,
  const unsigned numPoints,
  double * fieldPtr,
  const unsigned fieldSize,
  const unsigned /*beginPos*/,
  const unsigned /*endPos*/) const
{
  const double ymin = yTr_ - dTr_/2.0;
  const double ymax = yTr_ + dTr_/2.0;

  for(unsigned p=0; p < numPoints; ++p) {

    const double x = coords[0];
    const double y = coords[1];

    const double dy = -aX_*cos(2.0*tX_*pi_*x);
    const double yy = y + dy;
         
    double value = 0.0;
    if ( yy < ymin ) {
      value = 0.0;
    }
    else if (yy > ymax ) {
      value = 1.0;
    }
    else {
      value = surf_*1.0/2.0*(1.0 - sin(pi_*yy/dTr_));
    }

    fieldPtr[0] = value;

    fieldPtr += fieldSize;
    coords += spatialDimension;
  }
}

} // namespace nalu
} // namespace Sierra
