/*
  Time Integration

  Copyright 2010-201x held jointly by LANS/LANL, LBNL, and PNNL. 
  Amanzi is released under the three-clause BSD License. 
  The terms of use and "as is" disclaimer for this license are 
  provided in the top-level COPYRIGHT file.

  Author: Ethan Coon

  Simple timestep control based upon previous iteration count.
*/

#include "errors.hh"
#include "dbc.hh"
#include "TimestepControllerStandard.hh"

namespace Amanzi {

TimestepControllerStandard::TimestepControllerStandard(Teuchos::ParameterList& plist) :
    plist_(plist) {
  max_its_ = plist_.get<int>("max iterations");
  min_its_ = plist_.get<int>("min iterations");
  ASSERT(max_its_ > min_its_);
  ASSERT(min_its_ >= 0);

  reduction_factor_ = plist_.get<double>("time step reduction factor");
  ASSERT(reduction_factor_ >= 0.0);
  ASSERT(reduction_factor_ <= 1.0);

  increase_factor_ = plist_.get<double>("time step increase factor");
  ASSERT(increase_factor_ >= 1.0);

  max_dt_ = plist_.get<double>("max time step");
  min_dt_ = plist_.get<double>("min time step");
}


double
TimestepControllerStandard::get_timestep(double dt, int iterations) {
  double dt_next(dt);
  // iterations < 0 implies failed timestep
  if (iterations < 0 || iterations > max_its_) {
    dt_next = dt * reduction_factor_;
  } else if (iterations < min_its_) {
    dt_next = dt * increase_factor_;
  }

  // check max step size
  if (dt_next > max_dt_) dt_next = max_dt_;

  // check min step size
  if (dt_next < min_dt_) {
    if (iterations < 0) {
      std::string msg = "dT is too small, terminating...";
      Errors::Message m(msg);
      Exceptions::amanzi_throw(m);
    } else {
      dt_next = min_dt_;
    }
  }

  // check that if we have failed, our step size has decreased.
  if (iterations < 0) {
    if (dt - dt_next < 1.0e-10) {
      std::string msg = "dT change is too small, terminating...";
      Errors::Message m(msg);
      Exceptions::amanzi_throw(m);
    }
  }

  return dt_next;
}


} // namespace
