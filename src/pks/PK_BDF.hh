/*
  Process Kernels

  Copyright 2010-201x held jointly by LANS/LANL, LBNL, and PNNL. 
  Amanzi is released under the three-clause BSD License. 
  The terms of use and "as is" disclaimer for this license are 
  provided in the top-level COPYRIGHT file.

  Authors: Konstantin Lipnikov, Ethan Coon

  This is a purely virtual base class for process kernels which use
  time integrators.
*/

#ifndef AMANZI_FN_TIME_INTEGRATOR_PK_HH_
#define AMANZI_FN_TIME_INTEGRATOR_PK_HH_

#include "Teuchos_RCP.hpp"

#include "BDFFnBase.hh"
#include "PK.hh"


namespace Amanzi {

class TreeVector;
class PK_BDF : virtual public PK, public Amanzi::BDFFnBase<TreeVector> {};

}  // namespace Amanzi

#endif
