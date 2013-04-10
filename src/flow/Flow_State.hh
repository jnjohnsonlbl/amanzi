/*
This is the flow component of the Amanzi code. 
License: BSD
Authors: Konstantin Lipnikov (lipnikov@lanl.gov)

Routine provide basic operations with components of the flow state,
such as density, pressure, darcy mass flux, etc. 
Usage:
  Flow_State FS;       // for stand-alone initialization
  Flow_State FS(S);    // for initialization from the state S
  Flow_State FS(FS_);  // copy constructor
*/

#ifndef __FLOW_STATE_HH__
#define __FLOW_STATE_HH__

#include "Epetra_Vector.h"
#include "Epetra_CombineMode.h"
#include "Teuchos_RCP.hpp"

#include "Mesh.hh"
#include "State.hh"
#include "Flow_constants.hh"


namespace Amanzi {
namespace AmanziFlow {

class Flow_State {
 public:
  explicit Flow_State(const Teuchos::RCP<AmanziMesh::Mesh>& mesh);
  explicit Flow_State(const Teuchos::RCP<State>& S);
  explicit Flow_State(State& S);
  Flow_State(Flow_State& S, int mode = AmanziFlow::FLOW_STATE_VIEW);
  ~Flow_State() {};

  // data management
  void CopyMasterCell2GhostCell(Epetra_Vector& v);
  void CopyMasterCell2GhostCell(const Epetra_Vector& v, Epetra_Vector& vhost);
  void CopyMasterFace2GhostFace(Epetra_Vector& v);
  void CopyMasterFace2GhostFace(const Epetra_Vector& v, Epetra_Vector& vhost);
  void CopyMasterMultiCell2GhostMultiCell(Epetra_MultiVector& v);
  void CombineGhostFace2MasterFace(Epetra_Vector& v, Epetra_CombineMode mode = Insert);
  void CombineGhostCell2MasterCell(Epetra_Vector& v, Epetra_CombineMode mode = Insert);

  Epetra_Vector* CreateCellView(const Epetra_Vector& u) const;
  Epetra_Vector* CreateFaceView(const Epetra_Vector& u) const;

  // access methods
  Teuchos::RCP<AmanziGeometry::Point> gravity() { return gravity_; }  // RCP pointers

  Teuchos::RCP<double> fluid_density() { return fluid_density_; }
  Teuchos::RCP<double> fluid_viscosity() { return fluid_viscosity_; }
  Teuchos::RCP<Epetra_Vector> pressure() { return pressure_; }
  Teuchos::RCP<Epetra_Vector> lambda() { return lambda_; }
  Teuchos::RCP<Epetra_Vector> darcy_flux() { return darcy_flux_; }

  Teuchos::RCP<Epetra_Vector> vertical_permeability() { return vertical_permeability_; }
  Teuchos::RCP<Epetra_Vector> horizontal_permeability() { return horizontal_permeability_; }
  Teuchos::RCP<Epetra_Vector> porosity() { return porosity_; }
  Teuchos::RCP<Epetra_Vector> water_saturation() { return water_saturation_; }
  Teuchos::RCP<Epetra_Vector> prev_water_saturation() { return prev_water_saturation_; }

  Teuchos::RCP<Epetra_Vector> specific_storage() { return specific_storage_; }
  Teuchos::RCP<Epetra_Vector> specific_yield() { return specific_yield_; }
  Teuchos::RCP<const AmanziMesh::Mesh> mesh() { return mesh_; }

  State* state() { return S_; }

  double ref_fluid_density() { return *fluid_density_; }  // references
  double ref_fluid_viscosity() { return *fluid_viscosity_; }
  Epetra_Vector& ref_pressure() { return *pressure_; }
  Epetra_Vector& ref_lambda() { return *lambda_; }  
  Epetra_Vector& ref_darcy_flux() { return *darcy_flux_; }
  Epetra_MultiVector& ref_darcy_velocity() { return *darcy_velocity_; }
  const AmanziGeometry::Point& ref_gravity() { return *gravity_; }

  Epetra_Vector& ref_vertical_permeability() { return *vertical_permeability_; }
  Epetra_Vector& ref_horizontal_permeability() { return *horizontal_permeability_; }
  Epetra_Vector& ref_porosity() { return *porosity_; }
  Epetra_Vector& ref_water_saturation() { return *water_saturation_; }
  Epetra_Vector& ref_prev_water_saturation() { return *prev_water_saturation_; }

  Epetra_Vector& ref_specific_storage() { return *specific_storage_; }
  Epetra_Vector& ref_specific_yield() { return *specific_yield_; }

  // miscaleneous
  double get_time() { return (S_ == NULL) ? -1.0 : S_->get_time(); }
  double normLpCell(const Epetra_Vector& v1, double p);
  double normLpCell(const Epetra_Vector& v1, const Epetra_Vector& v2, double p);
  
  // debug routines
  void set_fluid_density(double rho);
  void set_fluid_viscosity(double mu);
  void set_porosity(double phi);
  void set_pressure_hydrostatic(double z0, double p0);
  void set_permeability(double Kh, double Kv);
  void set_permeability(double Kh, double Kv, const string region);
  void set_gravity(double g);
  void set_specific_storage(double ss);

 private:
  State* S_;  

  Teuchos::RCP<AmanziGeometry::Point> gravity_;

  Teuchos::RCP<double> fluid_density_;  // fluid properties
  Teuchos::RCP<double> fluid_viscosity_;
  Teuchos::RCP<Epetra_Vector> pressure_;
  Teuchos::RCP<Epetra_Vector> lambda_;
  Teuchos::RCP<Epetra_Vector> darcy_flux_;
  Teuchos::RCP<Epetra_MultiVector> darcy_velocity_;

  Teuchos::RCP<Epetra_Vector> vertical_permeability_;  // rock properties
  Teuchos::RCP<Epetra_Vector> horizontal_permeability_;
  Teuchos::RCP<Epetra_Vector> porosity_;
  Teuchos::RCP<Epetra_Vector> water_saturation_;
  Teuchos::RCP<Epetra_Vector> prev_water_saturation_;

  Teuchos::RCP<Epetra_Vector> specific_storage_;
  Teuchos::RCP<Epetra_Vector> specific_yield_;

  Teuchos::RCP<const AmanziMesh::Mesh> mesh_;
};

}  // namespace AmanziFlow
}  // namespace Amanzi

#endif