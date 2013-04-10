/*
This is the flow component of the Amanzi code. 

Copyright 2010-2012 held jointly by LANS/LANL, LBNL, and PNNL. 
Amanzi is released under the three-clause BSD License. 
The terms of use and "as is" disclaimer for this license are 
provided in the top-level COPYRIGHT file.

Authors: Konstantin Lipnikov (lipnikov@lanl.gov)
*/

#include <string>
#include <vector>

#include "Epetra_Vector.h"
#include "Epetra_MultiVector.h"
#include "Epetra_Import.h"

#include "Point.hh"
#include "errors.hh"
#include "Mesh.hh"

#include "State.hh"
#include "Flow_State.hh"

namespace Amanzi {
namespace AmanziFlow {

/* *******************************************************************
* Flow state is build from scratch and filled with zeros.         
******************************************************************* */
Flow_State::Flow_State(const Teuchos::RCP<AmanziMesh::Mesh>& mesh) : S_(NULL)
{
  int dim = mesh->space_dimension();
  const Epetra_BlockMap& cmap = mesh->cell_map(false);
  const Epetra_BlockMap& fmap = mesh->face_map(false);

  vertical_permeability_ = Teuchos::rcp(new Epetra_Vector(cmap));
  horizontal_permeability_ = Teuchos::rcp(new Epetra_Vector(cmap));
  porosity_ = Teuchos::rcp(new Epetra_Vector(cmap));
  water_saturation_ = Teuchos::rcp(new Epetra_Vector(cmap));
  prev_water_saturation_ = Teuchos::rcp(new Epetra_Vector(cmap));

  fluid_density_ = Teuchos::rcp(new double);
  fluid_viscosity_ = Teuchos::rcp(new double);
  pressure_ = Teuchos::rcp(new Epetra_Vector(cmap));
  lambda_ = Teuchos::rcp(new Epetra_Vector(fmap));
  darcy_flux_ = Teuchos::rcp(new Epetra_Vector(fmap));
  darcy_velocity_ = Teuchos::rcp(new Epetra_MultiVector(fmap, dim));

  gravity_ = Teuchos::rcp(new AmanziGeometry::Point(3));
  for (int i = 0; i < 3; i++) (*gravity_)[i] = 0.0;

  specific_storage_ = Teuchos::rcp(new Epetra_Vector(cmap));
  specific_yield_ = Teuchos::rcp(new Epetra_Vector(cmap));

  mesh_ = mesh;
}


/* *******************************************************************
* Flow state is build from the pointer to state S.        
******************************************************************* */
Flow_State::Flow_State(const Teuchos::RCP<State>& S) : S_(NULL)
{
  vertical_permeability_ = S->get_vertical_permeability();
  horizontal_permeability_ = S->get_horizontal_permeability();
  porosity_ = S->get_porosity();
  water_saturation_ = S->get_water_saturation();
  prev_water_saturation_ = S->get_prev_water_saturation();

  fluid_density_ = S->get_density();
  fluid_viscosity_ = S->get_viscosity();
  pressure_ = S->get_pressure();
  lambda_ = S->get_lambda();
  darcy_flux_ = S->get_darcy_flux();
  darcy_velocity_ = S->get_darcy_velocity();

  gravity_ = Teuchos::rcp(new AmanziGeometry::Point(3));
  for (int i = 0; i < 3; i++) (*gravity_)[i] = (*(S->get_gravity()))[i];

  specific_storage_ = S->get_specific_storage();
  specific_yield_ = S->get_specific_yield();

  mesh_ = S->get_mesh_maps();

  S_ = &*S;
}


/* *******************************************************************
* Flow state is build from state S.        
******************************************************************* */
Flow_State::Flow_State(State& S) : S_(NULL)
{
  vertical_permeability_ = S.get_vertical_permeability();
  horizontal_permeability_ = S.get_horizontal_permeability();
  porosity_ = S.get_porosity();
  water_saturation_ = S.get_water_saturation();
  prev_water_saturation_ = S.get_prev_water_saturation();

  fluid_density_ = S.get_density();
  fluid_viscosity_ = S.get_viscosity();
  pressure_ = S.get_pressure();
  lambda_ = S.get_lambda();
  darcy_flux_ = S.get_darcy_flux();
  darcy_velocity_ = S.get_darcy_velocity();

  gravity_ = Teuchos::rcp(new AmanziGeometry::Point(3));
  for (int i = 0; i < 3; i++) (*gravity_)[i] = (*(S.get_gravity()))[i];

  specific_storage_ = S.get_specific_storage();
  specific_yield_ = S.get_specific_yield();

  mesh_ = S.get_mesh_maps();

  S_ = &S;
}


/* *******************************************************************
* mode = FLOW_STATE_VIEW (default) a view of the given state           
* mode = FLOW_STATE_COPY allocates new memory for selected variable                    
******************************************************************* */
Flow_State::Flow_State(Flow_State& FS, int mode) : S_(NULL)
{
  vertical_permeability_ = FS.vertical_permeability();
  horizontal_permeability_ = FS.horizontal_permeability();
  porosity_ = FS.porosity();
  water_saturation_ = FS.water_saturation();
  prev_water_saturation_ = FS.prev_water_saturation();

  fluid_density_ = FS.fluid_density();
  fluid_viscosity_ = FS.fluid_viscosity();
  pressure_ = FS.pressure();
  lambda_ = FS.lambda();

  gravity_ = FS.gravity();
  specific_storage_ = FS.specific_storage();
  specific_yield_ = FS.specific_yield();

  mesh_ = FS.mesh();

  if (mode == AmanziFlow::FLOW_STATE_VIEW) {
    darcy_flux_ = FS.darcy_flux();
  } else if (mode == AmanziFlow::FLOW_STATE_COPY) {
    const Epetra_Map& fmap = mesh_->face_map(true);
    darcy_flux_ = Teuchos::rcp(new Epetra_Vector(fmap));
    CopyMasterFace2GhostFace(FS.ref_darcy_flux(), *darcy_flux_);
  }

  S_ = FS.S_;
}


/* *******************************************************************
* Copy cell-based data from master to ghost positions.              
* WARNING: vector v must contain ghost cells.              
******************************************************************* */
void Flow_State::CopyMasterCell2GhostCell(Epetra_Vector& v)
{
#ifdef HAVE_MPI
  const Epetra_BlockMap& source_cmap = mesh_->cell_map(false);
  const Epetra_BlockMap& target_cmap = mesh_->cell_map(true);
  Epetra_Import importer(target_cmap, source_cmap);

  double* vdata;
  v.ExtractView(&vdata);
  Epetra_Vector vv(View, source_cmap, vdata);

  v.Import(vv, importer, Insert);
#endif
}



/* *******************************************************************
* Copy cell-based data from master to ghost positions.              
* WARNING: MultiVector v must contain ghost cells.              
******************************************************************* */
void Flow_State::CopyMasterMultiCell2GhostMultiCell(Epetra_MultiVector& v)
{
#ifdef HAVE_MPI
  const Epetra_BlockMap& source_cmap = mesh_->cell_map(false);
  const Epetra_BlockMap& target_cmap = mesh_->cell_map(true);
  Epetra_Import importer(target_cmap, source_cmap);

  double** vdata;
  v.ExtractView(&vdata);
  Epetra_MultiVector vv(View, source_cmap, vdata, v.NumVectors());

  v.Import(vv, importer, Insert);
#endif
}


/* *******************************************************************
* Copy face-based data from master to ghost positions.              
* WARNING: vector v must contain ghost cells.              
******************************************************************* */
void Flow_State::CopyMasterFace2GhostFace(Epetra_Vector& v)
{
#ifdef HAVE_MPI
  const Epetra_BlockMap& source_cmap = mesh_->face_map(false);
  const Epetra_BlockMap& target_cmap = mesh_->face_map(true);
  Epetra_Import importer(target_cmap, source_cmap);

  double* vdata;
  v.ExtractView(&vdata);
  Epetra_Vector vv(View, source_cmap, vdata);

  v.Import(vv, importer, Insert);
#endif
}


/* *******************************************************************
* Transfers face-based data from ghost to master positions and 
* performs the operation 'mode' there. 
* WARNING: Vector v must contain ghost faces.              
******************************************************************* */
void Flow_State::CombineGhostFace2MasterFace(Epetra_Vector& v, Epetra_CombineMode mode)
{
#ifdef HAVE_MPI
  const Epetra_BlockMap& source_fmap = mesh_->face_map(false);
  const Epetra_BlockMap& target_fmap = mesh_->face_map(true);
  Epetra_Import importer(target_fmap, source_fmap);

  double* vdata;
  v.ExtractView(&vdata);
  Epetra_Vector vv(View, source_fmap, vdata);

  vv.Export(v, importer, mode);
#endif
}


/* *******************************************************************
* Copy face-based data from master to ghost positions.              
* WARNING: vector vghost must contain ghost cells.              
******************************************************************* */
void Flow_State::CopyMasterCell2GhostCell(const Epetra_Vector& v, Epetra_Vector& vghost)
{
#ifdef HAVE_MPI
  const Epetra_BlockMap& source_cmap = mesh_->cell_map(false);
  const Epetra_BlockMap& target_cmap = mesh_->cell_map(true);
  Epetra_Import importer(target_cmap, source_cmap);
 
  vghost.Import(v, importer, Insert);
#else
  vghost = v;
#endif
}


/* *******************************************************************
* Transfers cell-based data from ghost to master positions and 
* performs the operation 'mode' there. 
* WARNING: Vector v must contain ghost cells.              
******************************************************************* */
void Flow_State::CombineGhostCell2MasterCell(Epetra_Vector& v, Epetra_CombineMode mode)
{
#ifdef HAVE_MPI
  const Epetra_BlockMap& source_cmap = mesh_->cell_map(false);
  const Epetra_BlockMap& target_cmap = mesh_->cell_map(true);
  Epetra_Import importer(target_cmap, source_cmap);

  double* vdata;
  v.ExtractView(&vdata);
  Epetra_Vector vv(View, source_cmap, vdata);

  vv.Export(v, importer, mode);
#endif
}


/* *******************************************************************
* Copy face-based data from master to ghost positions.              
* WARNING: vector vhost must contain ghost cells.              
******************************************************************* */
void Flow_State::CopyMasterFace2GhostFace(const Epetra_Vector& v, Epetra_Vector& vghost)
{
#ifdef HAVE_MPI
  const Epetra_BlockMap& source_cmap = mesh_->face_map(false);
  const Epetra_BlockMap& target_cmap = mesh_->face_map(true);
  Epetra_Import importer(target_cmap, source_cmap);
 
  vghost.Import(v, importer, Insert);
#else
  vghost = v;
#endif
}


/* *******************************************************************
* Lp norm of the vector v1.    
******************************************************************* */
double Flow_State::normLpCell(const Epetra_Vector& v1, double p)
{
  int ncells = (mesh_->cell_map(false)).NumMyElements();

  double Lp_norm, Lp = 0.0;
  for (int c = 0; c < ncells; c++) {
    double volume = mesh_->cell_volume(c);
    Lp += volume * pow(v1[c], p);
  }
  v1.Comm().MaxAll(&Lp, &Lp_norm, 1);

  return pow(Lp_norm, 1.0/p);
}


/* *******************************************************************
* Lp norm of the component-wise product v1 .* v2             
******************************************************************* */
double Flow_State::normLpCell(const Epetra_Vector& v1, const Epetra_Vector& v2, double p)
{
  int ncells = (mesh_->cell_map(false)).NumMyElements();

  double Lp_norm, Lp = 0.0;
  for (int c = 0; c < ncells; c++) {
    double volume = mesh_->cell_volume(c);
    Lp += volume * pow(v1[c] * v2[c], p);
  }
  v1.Comm().MaxAll(&Lp, &Lp_norm, 1);

  return pow(Lp_norm, 1.0/p);
}


/* *******************************************************************
* Extract cells from a supervector             
******************************************************************* */
Epetra_Vector* Flow_State::CreateCellView(const Epetra_Vector& u) const
{
  double* data;
  u.ExtractView(&data);
  return new Epetra_Vector(View, mesh_->cell_map(false), data);
}


/* *******************************************************************
* Extract faces from a supervector             
******************************************************************* */
Epetra_Vector* Flow_State::CreateFaceView(const Epetra_Vector& u) const
{
  double* data;
  u.ExtractView(&data);
  int ncells = (mesh_->cell_map(false)).NumMyElements();
  return new Epetra_Vector(View, mesh_->face_map(false), data+ncells);
}


/* *******************************************************************
* DEBUG: create constant fluid density. Since it is debug, we
* do not verify that rho is positive.    
******************************************************************* */
void Flow_State::set_fluid_density(double rho)
{
  *fluid_density_ = rho;
}


/* *******************************************************************
* DEBUG: create constant fluid viscosity
******************************************************************* */
void Flow_State::set_fluid_viscosity(double mu)
{
  *fluid_viscosity_ = mu;
}


/* *******************************************************************
* DEBUG: create constant porosity
******************************************************************* */
void Flow_State::set_porosity(double phi)
{
  porosity_->PutScalar(phi);
}


/* *******************************************************************
* DEBUG: create hydrostatic pressure with p0 at height z0.
******************************************************************* */
void Flow_State::set_pressure_hydrostatic(double z0, double p0)
{
  int dim = mesh_->space_dimension();
  int ncells = mesh_->num_entities(AmanziMesh::CELL, AmanziMesh::USED);

  double rho = *fluid_density_;
  double g = (*gravity_)[dim - 1];

  for (int c = 0; c < ncells; c++) {
    const AmanziGeometry::Point& xc = mesh_->cell_centroid(c);
    (*pressure_)[c] = p0 + rho * g * (xc[dim - 1] - z0);
  }
}


/* *******************************************************************
 * DEBUG: create diagonal permeability
 ****************************************************************** */
void Flow_State::set_permeability(double Kh, double Kv)
{
  horizontal_permeability_->PutScalar(Kh);
  vertical_permeability_->PutScalar(Kv);
}

void Flow_State::set_permeability(double Kh, double Kv, const string region)
{
  AmanziMesh::Entity_ID_List block;
  mesh_->get_set_entities(region, AmanziMesh::CELL, AmanziMesh::OWNED, &block);
  int ncells = block.size();

  for (int i = 0; i < ncells; i++) {
    int c = block[i];
    (*horizontal_permeability_)[c] = Kh;
    (*vertical_permeability_)[c] = Kv;
  }
}


/* *******************************************************************
 * DEBUG: create constant porosity
 ****************************************************************** */
void Flow_State::set_specific_storage(double ss)
{
  specific_storage_->PutScalar(ss);
}


/* *******************************************************************
 * DEBUG: create constant gravity
 ****************************************************************** */
void Flow_State::set_gravity(double g)
{
  int dim = mesh_->space_dimension();
  for (int i = 0; i < dim-1; i++) (*gravity_)[i] = 0.0;
  (*gravity_)[dim-1] = g;
  (*gravity_)[2] = g;  // Waiting for Markus ticket (lipnikov@lanl.gov)
}


}  // namespace AmanziTransport
}  // namespace Amanzi
