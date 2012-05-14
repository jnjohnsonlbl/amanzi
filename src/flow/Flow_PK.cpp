/*
This is the flow component of the Amanzi code. 

Copyright 2010-2012 held jointly by LANS/LANL, LBNL, and PNNL. 
Amanzi is released under the three-clause BSD License. 
The terms of use and "as is" disclaimer for this license are 
provided in the top-level COPYRIGHT file.

Authors: Neil Carlson (version 1) 
         Konstantin Lipnikov (version 2) (lipnikov@lanl.gov)
*/

#include <string>
#include <vector>

#include "Teuchos_ParameterList.hpp"
// #include <boost/timer.hpp>

#include "Mesh.hh"
#include "gmv_mesh.hh"

#include "mfd3d.hpp"
#include "Flow_PK.hpp"
#include "Flow_State.hpp"

namespace Amanzi {
namespace AmanziFlow {

/* ******************************************************************
* Initiazition of fundamental flow sturctures.                                              
****************************************************************** */
void Flow_PK::Init(Teuchos::RCP<Flow_State> FS_MPC)
{
  flow_status_ = FLOW_STATUS_NULL;

  FS = Teuchos::rcp(new Flow_State(*FS_MPC));
  mesh_ = FS->mesh();
  dim = mesh_->space_dimension();
  MyPID = 0;

  T_internal = dT = 0.0;
  standalone_mode = false;

  ncells_owned = mesh_->count_entities(AmanziMesh::CELL, AmanziMesh::OWNED);
  ncells_wghost = mesh_->count_entities(AmanziMesh::CELL, AmanziMesh::USED);

  nfaces_owned = mesh_->count_entities(AmanziMesh::FACE, AmanziMesh::OWNED);
  nfaces_wghost = mesh_->count_entities(AmanziMesh::FACE, AmanziMesh::USED);
}


/* ******************************************************************
* Super-map combining cells and faces.                                                  
****************************************************************** */
Epetra_Map* Flow_PK::createSuperMap()
{
  const Epetra_Map& cmap = mesh_->cell_map(false);
  const Epetra_Map& fmap = mesh_->face_map(false);

  int ndof = ncells_owned + nfaces_owned;
  int ndof_global_cell = cmap.NumGlobalElements();
  int ndof_global = ndof_global_cell + fmap.NumGlobalElements();

  int* gids = new int[ndof];
  cmap.MyGlobalElements(&(gids[0]));
  fmap.MyGlobalElements(&(gids[ncells_owned]));

  for (int f = 0; f < nfaces_owned; f++) gids[ncells_owned + f] += ndof_global_cell;
  Epetra_Map* map = new Epetra_Map(ndof_global, ndof, gids, 0, cmap.Comm());

  delete [] gids;
  return map;
}


/* ******************************************************************
* Add a boundary marker to used faces.                                          
****************************************************************** */
void Flow_PK::UpdateBoundaryConditions(
    BoundaryFunction* bc_pressure, BoundaryFunction* bc_head,
    BoundaryFunction* bc_flux, BoundaryFunction* bc_seepage,
    const Epetra_Vector& pressure_cells, const double atm_pressure,
    std::vector<int>& bc_markers, std::vector<double>& bc_values)
{
  int flag_essential_bc = 0;
  for (int n = 0; n < bc_markers.size(); n++) {
    bc_markers[n] = FLOW_BC_FACE_NULL;
    bc_values[n] = 0.0;
  }

  Amanzi::Iterator bc;
  for (bc = bc_pressure->begin(); bc != bc_pressure->end(); ++bc) {
    int f = bc->first;
    bc_markers[f] = FLOW_BC_FACE_PRESSURE;
    bc_values[f] = bc->second;
    flag_essential_bc = 1;
  }

  for (bc = bc_head->begin(); bc != bc_head->end(); ++bc) {
    int f = bc->first;
    bc_markers[f] = FLOW_BC_FACE_HEAD;
    bc_values[f] = bc->second;
    flag_essential_bc = 1;
  }

  for (bc = bc_flux->begin(); bc != bc_flux->end(); ++bc) {
    int f = bc->first;
    bc_markers[f] = FLOW_BC_FACE_FLUX;
    bc_values[f] = bc->second;
  }

  AmanziMesh::Entity_ID_List cells;
  for (bc = bc_seepage->begin(); bc != bc_seepage->end(); ++bc) {
    int f = bc->first;
    mesh_->face_get_cells(f, AmanziMesh::OWNED, &cells);
    int c = cells[0];  // Assume that face and cell are on the same processor.

    if (pressure_cells[c] < atm_pressure) {
      bc_markers[f] = FLOW_BC_FACE_FLUX;
      bc_values[f] = bc->second;
    } else {
      bc_markers[f] = FLOW_BC_FACE_PRESSURE;
      bc_values[f] = atm_pressure;
      flag_essential_bc = 1;
    }
  }

  // mark missing boundary conditions as zero flux conditions
  int missed = 0;
  for (int f = 0; f < nfaces_owned; f++) {
    if (bc_markers[f] == FLOW_BC_FACE_NULL) {
      cells.clear();
      mesh_->face_get_cells(f, AmanziMesh::USED, &cells);
      int ncells = cells.size();

      if (ncells == 1) {
        bc_markers[f] = FLOW_BC_FACE_FLUX;
        bc_values[f] = 0.0;
        missed++;
      }
    }
  }
  if (MyPID == 0 && verbosity >= FLOW_VERBOSITY_EXTREME && missed > 0) {
    std::printf("Richards Flow: assigned zero flux boundary condition to%7d faces\n", missed);
  }

  // verify that the algebraic problem is consistent
#ifdef HAVE_MPI
  int flag = flag_essential_bc;
  mesh_->get_comm()->MaxAll(&flag, &flag_essential_bc, 1);  // find the global maximum
#endif
  if (! flag_essential_bc) {
     Errors::Message msg; 
     msg << "Flow PK: No essential boundary conditions, the solver may fail.";
     Exceptions::amanzi_throw(msg);
  }
}


/* ******************************************************************
* Add a boundary marker to owned faces.                                          
****************************************************************** */
void Flow_PK::applyBoundaryConditions(std::vector<int>& bc_markers,
                                      std::vector<double>& bc_values,
                                      Epetra_Vector& pressure_faces)
{
  int nfaces = mesh_->num_entities(AmanziMesh::FACE, AmanziMesh::OWNED);
  for (int f = 0; f < nfaces; f++) {
    if (bc_markers[f] == FLOW_BC_FACE_PRESSURE ||
        bc_markers[f] == FLOW_BC_FACE_HEAD) {
      pressure_faces[f] = bc_values[f];
    }
  }
}


/* ******************************************************************
* Add source and sink terms. We use a simplified algorithms than for
* boundary conditions.                                          
****************************************************************** */
void Flow_PK::addSourceTerms(DomainFunction* src_sink, Epetra_Vector& rhs)
{
  Amanzi::Iterator src;
  for (src = src_sink->begin(); src != src_sink->end(); ++src) {
    int c = src->first;
    rhs[c] += mesh_->cell_volume(c) * src->second;
  }
}


/* ******************************************************************
* Routine updates elemental discretization matrices and must be 
* called before applying boundary conditions and global assembling.                                             
****************************************************************** */
void Flow_PK::addGravityFluxes_MFD(std::vector<WhetStone::Tensor>& K,
                                   const Epetra_Vector& Krel_faces,
                                   Matrix_MFD* matrix)
{
  double rho = FS->ref_fluid_density();
  AmanziGeometry::Point gravity(dim);
  for (int k = 0; k < dim; k++) gravity[k] = (*(FS->gravity()))[k] * rho;

  AmanziMesh::Entity_ID_List faces;
  std::vector<int> dirs;

  for (int c = 0; c < ncells_owned; c++) {
    mesh_->cell_get_faces_and_dirs(c, &faces, &dirs);
    int nfaces = faces.size();

    Epetra_SerialDenseVector& Ff = matrix->Ff_cells()[c];
    double& Fc = matrix->Fc_cells()[c];

    for (int n = 0; n < nfaces; n++) {
      int f = faces[n];
      const AmanziGeometry::Point& normal = mesh_->face_normal(f);

      double outward_flux = ((K[c] * gravity) * normal) * dirs[n] * Krel_faces[f];
      Ff[n] += outward_flux;
      Fc -= outward_flux;  // Nonzero-sum contribution when flag_upwind = false.
    }
  }
}


/* ******************************************************************
* Updates global Darcy vector calculated by a discretization method.                                             
****************************************************************** */
void Flow_PK::addGravityFluxes_DarcyFlux(std::vector<WhetStone::Tensor>& K,
                                         const Epetra_Vector& Krel_faces,
                                         Epetra_Vector& darcy_mass_flux)
{
  double rho = FS->ref_fluid_density();
  AmanziGeometry::Point gravity(dim);
  for (int k = 0; k < dim; k++) gravity[k] = (*(FS->gravity()))[k] * rho;

  AmanziMesh::Entity_ID_List faces;
  std::vector<int> dirs;
  std::vector<int> flag(nfaces_wghost, 0);

  for (int c = 0; c < ncells_owned; c++) {
    mesh_->cell_get_faces_and_dirs(c, &faces, &dirs);
    int nfaces = faces.size();

    for (int n = 0; n < nfaces; n++) {
      int f = faces[n];
      const AmanziGeometry::Point& normal = mesh_->face_normal(f);

      if (f < nfaces_owned && !flag[f]) {
        darcy_mass_flux[f] += ((K[c] * gravity) * normal) * Krel_faces[f];
        flag[f] = 1;
      }
    }
  }
}


/* *******************************************************************
* Identify flux direction based on orientation of the face normal 
* and sign of the Darcy velocity. 
* WARNING: It is *not* used now.                              
******************************************************************* */
void Flow_PK::identifyUpwindCells(Epetra_IntVector& upwind_cell, Epetra_IntVector& downwind_cell)
{
  for (int f = 0; f < nfaces_owned; f++) {
    upwind_cell[f] = -1;  // negative value is indicator of a boundary
    downwind_cell[f] = -1;
  }

  Epetra_Vector& darcy_flux = FS->ref_darcy_flux();
  AmanziMesh::Entity_ID_List faces;
  std::vector<int> fdirs;

  for (int c = 0; c < ncells_owned; c++) {
    mesh_->cell_get_faces_and_dirs(c, &faces, &fdirs);

    for (int i = 0; i < faces.size(); i++) {
      int f = faces[i];
      if (darcy_flux[f] * fdirs[i] >= 0)
        upwind_cell[f] = c;
      else
        downwind_cell[f] = c;
    }
  }
}


/* ****************************************************************
* DEBUG: creating GMV file 
**************************************************************** */
void Flow_PK::writeGMVfile(Teuchos::RCP<Flow_State> FS) const
{
  Teuchos::RCP<AmanziMesh::Mesh> mesh = FS->mesh();

  GMV::open_data_file(*mesh, (std::string)"flow.gmv");
  GMV::start_data();
  GMV::write_cell_data(FS->ref_pressure(), "pressure");
  GMV::write_cell_data(FS->ref_water_saturation(), "saturation");
  GMV::write_cell_data(FS->ref_darcy_velocity(), 0, "velocity_h");
  GMV::write_cell_data(FS->ref_darcy_velocity(), dim-1, "velocity_v");
  GMV::close_data_file();
}

}  // namespace AmanziFlow
}  // namespace Amanzi
