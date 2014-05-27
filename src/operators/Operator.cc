/*
  This is the Operator component of the Amanzi code.

  License: BSD
  Authors: Konstantin Lipnikov (lipnikov@lanl.gov)
*/

#include "Epetra_Map.h"
#include "Epetra_Vector.h"
#include "Epetra_Export.h"
#include "Epetra_CrsGraph.h"
#include "Epetra_FECrsGraph.h"

#include "DenseVector.hh"
#include "PreconditionerFactory.hh"
#include "Operator.hh"
#include "OperatorDefs.hh"

namespace Amanzi {
namespace Operators {

/* ******************************************************************
* Default constructor.
****************************************************************** */
Operator::Operator(Teuchos::RCP<const CompositeVectorSpace> cvs, int dummy) 
    : cvs_(cvs), data_validity_(true) 
{
  mesh_ = cvs_->Mesh();
  rhs_ = Teuchos::rcp(new CompositeVector(*cvs_, true));
  diagonal_ = Teuchos::rcp(new CompositeVector(*cvs_, true));

  ncells_owned = mesh_->num_entities(AmanziMesh::CELL, AmanziMesh::OWNED);
  nfaces_owned = mesh_->num_entities(AmanziMesh::FACE, AmanziMesh::OWNED);
  nnodes_owned = mesh_->num_entities(AmanziMesh::NODE, AmanziMesh::OWNED);

  ncells_wghost = mesh_->num_entities(AmanziMesh::CELL, AmanziMesh::USED);
  nfaces_wghost = mesh_->num_entities(AmanziMesh::FACE, AmanziMesh::USED);
  nnodes_wghost = mesh_->num_entities(AmanziMesh::NODE, AmanziMesh::USED);
}


/* ******************************************************************
* Second constructor.
****************************************************************** */
Operator::Operator(const Operator& op) 
    : data_validity_(true), 
      mesh_(op.mesh_),
      cvs_(op.cvs_), 
      blocks_(op.blocks_), 
      blocks_schema_(op.blocks_schema_), 
      blocks_shadow_(op.blocks_shadow_), 
      diagonal_(op.diagonal_),
      rhs_(op.rhs_),
      A_(op.A_),
      preconditioner_(op.preconditioner_)
{
  op.data_validity_ = false;
  ncells_owned = mesh_->num_entities(AmanziMesh::CELL, AmanziMesh::OWNED);
  nfaces_owned = mesh_->num_entities(AmanziMesh::FACE, AmanziMesh::OWNED);
  nnodes_owned = mesh_->num_entities(AmanziMesh::NODE, AmanziMesh::OWNED);

  ncells_wghost = mesh_->num_entities(AmanziMesh::CELL, AmanziMesh::USED);
  nfaces_wghost = mesh_->num_entities(AmanziMesh::FACE, AmanziMesh::USED);
  nnodes_wghost = mesh_->num_entities(AmanziMesh::NODE, AmanziMesh::USED);

  for (int i = 0; i < 3; i++) { 
    offset_global_[i] = op.offset_global_[i];
    offset_my_[i] = op.offset_my_[i];
  }
}


/* ******************************************************************
* Copy data structures from another operator.
****************************************************************** */
void Operator::Clone(const Operator& op) 
{
  data_validity_ = true; 
  op.data_validity_ = false;
  mesh_ = op.mesh_;
  cvs_ = op.cvs_; 
  blocks_ = op.blocks_;
  blocks_schema_ = op.blocks_schema_;
  blocks_shadow_ = op.blocks_shadow_; 
  diagonal_ = op.diagonal_;
  rhs_ = op.rhs_;
  A_ = op.A_;
  preconditioner_ = op.preconditioner_;

  ncells_owned = mesh_->num_entities(AmanziMesh::CELL, AmanziMesh::OWNED);
  nfaces_owned = mesh_->num_entities(AmanziMesh::FACE, AmanziMesh::OWNED);
  nnodes_owned = mesh_->num_entities(AmanziMesh::NODE, AmanziMesh::OWNED);

  ncells_wghost = mesh_->num_entities(AmanziMesh::CELL, AmanziMesh::USED);
  nfaces_wghost = mesh_->num_entities(AmanziMesh::FACE, AmanziMesh::USED);
  nnodes_wghost = mesh_->num_entities(AmanziMesh::NODE, AmanziMesh::USED);

  for (int i = 0; i < 3; i++) { 
    offset_global_[i] = op.offset_global_[i];
    offset_my_[i] = op.offset_my_[i];
  }
}


/* ******************************************************************
* Initialization of the operator.                                           
****************************************************************** */
void Operator::Init()
{
  diagonal_->PutScalarMasterAndGhosted(0.0);
  rhs_->PutScalarMasterAndGhosted(0.0);

  int n = blocks_.size();
  for (int i = 0; i < n; i++) { 
    std::vector<WhetStone::DenseMatrix>& matrix = *blocks_[i];
    int m = matrix.size();
    for (int k = 0; k < m; k++) {
      matrix[k] = 0.0;
    }
  }
}


#ifdef OPERATORS_MATRIX_FE_CRS
/* ******************************************************************
* Create a global matrix.
****************************************************************** */
void Operator::SymbolicAssembleMatrix(int schema)
{
  // create global Epetra map
  const Epetra_Map& cmap = mesh_->cell_map(false);
  const Epetra_Map& fmap = mesh_->face_map(false);
  const Epetra_Map& vmap = mesh_->node_map(false);

  int ndof(0), ndof_global(0), offset(0);

  if (schema & OPERATOR_SCHEMA_DOFS_FACE) ndof += nfaces_owned;
  if (schema & OPERATOR_SCHEMA_DOFS_CELL) ndof += ncells_owned;
  if (schema & OPERATOR_SCHEMA_DOFS_NODE) ndof += nnodes_owned;

  int* gids = new int[ndof];

  offset_global_[0] = 0;
  offset_my_[0] = 0;
  if (schema & OPERATOR_SCHEMA_DOFS_FACE) {
    fmap.MyGlobalElements(&(gids[0]));
    offset += nfaces_owned;
    ndof_global += fmap.NumGlobalElements();
  } 

  offset_global_[1] = ndof_global;
  offset_my_[1] = offset;
  if (schema & OPERATOR_SCHEMA_DOFS_CELL) {
    cmap.MyGlobalElements(&(gids[offset]));
    for (int c = 0; c < ncells_owned; c++) gids[offset + c] += ndof_global;
    offset += ncells_owned;
    ndof_global += cmap.NumGlobalElements();
  }

  offset_global_[2] = ndof_global;
  offset_my_[2] = offset;
  if (schema & OPERATOR_SCHEMA_DOFS_NODE) {
    vmap.MyGlobalElements(&(gids[offset]));
    for (int v = 0; v < nnodes_owned; v++) gids[offset + v] += ndof_global;
    offset += nnodes_owned;
    ndof_global += vmap.NumGlobalElements();
  }

  Epetra_Map* map = new Epetra_Map(ndof_global, ndof, gids, 0, cmap.Comm());
  delete [] gids;

  // estimate size of the matrix graph
  const Epetra_Map& cmap_wghost = mesh_->cell_map(true);
  const Epetra_Map& fmap_wghost = mesh_->face_map(true);
  const Epetra_Map& vmap_wghost = mesh_->node_map(true);

  int row_size(0), dim = mesh_->space_dimension();
  if (schema & OPERATOR_SCHEMA_DOFS_FACE) {
    int i = (dim == 2) ? OPERATOR_QUAD_FACES : OPERATOR_HEX_FACES;
    row_size += 2 * i;
  }
  if (schema & OPERATOR_SCHEMA_DOFS_CELL) {
    int i = (dim == 2) ? OPERATOR_QUAD_FACES : OPERATOR_HEX_FACES;
    row_size += i + 1;
  }
  if (schema & OPERATOR_SCHEMA_DOFS_NODE) {
    int i = (dim == 2) ? OPERATOR_QUAD_NODES : OPERATOR_HEX_NODES;
    row_size += 8 * i;
  }
    
  Epetra_FECrsGraph ff_graph(Copy, *map, row_size);
  delete map;

  // populate matrix graph using blocks that fit the schema
  AmanziMesh::Entity_ID_List cells, faces, nodes;
  int gid[OPERATOR_MAX_NODES];

  int nblocks = blocks_.size();
  for (int nb = 0; nb < nblocks; nb++) {
    int subschema = blocks_schema_[nb] & schema;

    if (blocks_schema_[nb] & OPERATOR_SCHEMA_BASE_CELL) {
      for (int c = 0; c < ncells_owned; c++) {
        int nd;
        if (subschema == OPERATOR_SCHEMA_DOFS_FACE) {
          mesh_->cell_get_faces(c, &faces);
          int nfaces = faces.size();

          for (int n = 0; n < nfaces; n++) {
            gid[n] = fmap_wghost.GID(faces[n]);
          }
          nd = nfaces;
        } else if (subschema == OPERATOR_SCHEMA_DOFS_FACE + OPERATOR_SCHEMA_DOFS_CELL) {
          mesh_->cell_get_faces(c, &faces);
          int nfaces = faces.size();

          for (int n = 0; n < nfaces; n++) {
            gid[n] = fmap_wghost.GID(faces[n]);
          }
          gid[nfaces] = offset_global_[1] + cmap_wghost.GID(c);

          nd = nfaces + 1;
        } else if (subschema == OPERATOR_SCHEMA_DOFS_NODE) {
          mesh_->cell_get_nodes(c, &nodes);
          int nnodes = nodes.size();

          for (int n = 0; n < nnodes; n++) {
            gid[n] = offset_global_[2] + vmap_wghost.GID(nodes[n]);
          }
          nd = nnodes;
        }

        ff_graph.InsertGlobalIndices(nd, gid, nd, gid);
      }
    } else if (blocks_schema_[nb] & OPERATOR_SCHEMA_BASE_FACE) {
      for (int f = 0; f < nfaces_owned; f++) {
        mesh_->face_get_cells(f, AmanziMesh::USED, &cells);
        int ncells = cells.size();

        int nd;
        if (subschema == OPERATOR_SCHEMA_DOFS_CELL) {
          for (int n = 0; n < ncells; n++) {
            gid[n] = offset_global_[1] + cmap_wghost.GID(cells[n]);
          }
          nd = ncells;
        }

        ff_graph.InsertGlobalIndices(nd, gid, nd, gid);
      }
    } else if (blocks_schema_[nb] & OPERATOR_SCHEMA_BASE_NODE) {
      // not implemented yet
    }
  }

  ff_graph.GlobalAssemble();  // Symbolic graph is complete.

  // create global matrix
  A_ = Teuchos::rcp(new Epetra_FECrsMatrix(Copy, ff_graph));
  A_->GlobalAssemble();
}


#else
/* ******************************************************************
* Create a global matrix.
****************************************************************** */
void Operator::SymbolicAssembleMatrix(int schema)
{
  const Epetra_Map& cmap = mesh_->cell_map(false);
  const Epetra_Map& fmap = mesh_->face_map(false);
  const Epetra_Map& vmap = mesh_->node_map(false);

  // cound local and global DOFs
  int ndof(0), ndof_off(0), ndof_global(0), offset(0);

  if (schema & OPERATOR_SCHEMA_DOFS_FACE) {
    ndof += nfaces_owned;
    ndof_off += nfaces_wghost - nfaces_owned;
  }
  if (schema & OPERATOR_SCHEMA_DOFS_CELL) {
    ndof += ncells_owned;
    ndof_off += ncells_wghost - ncells_owned;
  }
  if (schema & OPERATOR_SCHEMA_DOFS_NODE) {
    ndof += nnodes_owned;
    ndof_off += nnodes_wghost - nnodes_owned;
  }

  // create new global map (supermap)
  int* gids = new int[ndof + ndof_off];

  offset_global_[0] = 0;
  offset_my_[0] = 0;
  if (schema & OPERATOR_SCHEMA_DOFS_FACE) {
    fmap.MyGlobalElements(&(gids[0]));
    offset += nfaces_owned;
    ndof_global += fmap.NumGlobalElements();
  } 

  offset_global_[1] = ndof_global;
  offset_my_[1] = offset;
  if (schema & OPERATOR_SCHEMA_DOFS_CELL) {
    cmap.MyGlobalElements(&(gids[offset]));
    for (int c = 0; c < ncells_owned; c++) gids[offset + c] += ndof_global;
    offset += ncells_owned;
    ndof_global += cmap.NumGlobalElements();
  }

  offset_global_[2] = ndof_global;
  offset_my_[2] = offset;
  if (schema & OPERATOR_SCHEMA_DOFS_NODE) {
    vmap.MyGlobalElements(&(gids[offset]));
    for (int v = 0; v < nnodes_owned; v++) gids[offset + v] += ndof_global;
    offset += nnodes_owned;
    ndof_global += vmap.NumGlobalElements();
  }

  // add ghosts to the global map
  const Epetra_Map& cmap_wghost = mesh_->cell_map(true);
  const Epetra_Map& fmap_wghost = mesh_->face_map(true);
  const Epetra_Map& vmap_wghost = mesh_->node_map(true);

  int n(ndof);
  if (schema & OPERATOR_SCHEMA_DOFS_FACE) {
    for (int f = nfaces_owned; f < nfaces_wghost; f++) {
      gids[n++] = fmap_wghost.GID(f);
    }
  } 

  if (schema & OPERATOR_SCHEMA_DOFS_CELL) {
    for (int c = ncells_owned; c < ncells_wghost; c++) { 
      gids[n++] = cmap_wghost.GID(c) + offset_global_[1];
    }
  }

  if (schema & OPERATOR_SCHEMA_DOFS_NODE) {
    for (int v = nnodes_owned; v < nnodes_wghost; v++) {
      gids[n++] = vmap_wghost.GID(v) + offset_global_[2];
    }
  }

  Epetra_Map* map = new Epetra_Map(ndof_global, ndof, gids, 0, cmap.Comm());
  Epetra_Map* map_wghost = new Epetra_Map(-1, ndof + ndof_off, gids, 0, cmap.Comm());
  Epetra_Map* map_off = new Epetra_Map(-1, ndof_off, &(gids[ndof]), 0, cmap.Comm());
  delete [] gids;

  // estimate size of the matrix graph
  int row_size(0), dim = mesh_->space_dimension();
  if (schema & OPERATOR_SCHEMA_DOFS_FACE) {
    int i = (dim == 2) ? OPERATOR_QUAD_FACES : OPERATOR_HEX_FACES;

    for (int c = 0; c < ncells_owned; c++) {
      int nfaces = mesh_->cell_get_num_faces(c);
      i = std::max(i, nfaces);
    }
    row_size += 2 * i;
  }
  if (schema & OPERATOR_SCHEMA_DOFS_CELL) {
    int i = (dim == 2) ? OPERATOR_QUAD_FACES : OPERATOR_HEX_FACES;
    row_size += i + 1;
  }
  if (schema & OPERATOR_SCHEMA_DOFS_NODE) {
    int i = (dim == 2) ? OPERATOR_QUAD_NODES : OPERATOR_HEX_NODES;
    row_size += 8 * i;
  }
    
  // create local and global maps
  Epetra_CrsGraph ff_graph = Epetra_CrsGraph(Copy, *map, *map_wghost, row_size, false);
  Epetra_CrsGraph ff_graph_off = Epetra_CrsGraph(Copy, *map_off, *map_wghost, row_size, false);

  // populate matrix graph using blocks that fit the schema
  AmanziMesh::Entity_ID_List cells, faces, nodes;
  int lid[OPERATOR_MAX_NODES];
  int gid[OPERATOR_MAX_NODES];

  int nblocks = blocks_.size();
  for (int nb = 0; nb < nblocks; nb++) {
    int subschema = blocks_schema_[nb] & schema;

    if (blocks_schema_[nb] & OPERATOR_SCHEMA_BASE_CELL) {
      for (int c = 0; c < ncells_owned; c++) {
        int nd;
        if (subschema == OPERATOR_SCHEMA_DOFS_FACE) {
          mesh_->cell_get_faces(c, &faces);
          int nfaces = faces.size();

          for (int n = 0; n < nfaces; n++) {
            lid[n] = faces[n];
            gid[n] = fmap_wghost.GID(faces[n]);
          }
          nd = nfaces;
        } else if (subschema == OPERATOR_SCHEMA_DOFS_FACE + OPERATOR_SCHEMA_DOFS_CELL) {
          mesh_->cell_get_faces(c, &faces);
          int nfaces = faces.size();

          for (int n = 0; n < nfaces; n++) {
            lid[n] = faces[n];
            if (faces[n] >= nfaces_owned) lid[n] += ndof - nfaces_owned; 
            gid[n] = fmap_wghost.GID(faces[n]);
          }
          lid[nfaces] = offset_my_[1] + c;
          gid[nfaces] = offset_global_[1] + cmap_wghost.GID(c);

          nd = nfaces + 1;
        } else if (subschema == OPERATOR_SCHEMA_DOFS_NODE) {
          mesh_->cell_get_nodes(c, &nodes);
          int nnodes = nodes.size();

          for (int n = 0; n < nnodes; n++) {
            lid[n] = offset_my_[2] + nodes[n];
            gid[n] = offset_global_[2] + vmap_wghost.GID(nodes[n]);
          }
          nd = nnodes;
        }

        for (int n = 0; n < nd; n++) {
          if (lid[n] < ndof) {
            ff_graph.InsertGlobalIndices(gid[n], nd, gid);
          } else {
            ff_graph_off.InsertMyIndices(lid[n] - ndof, nd, lid);
          }
        }
      }
    } else if (blocks_schema_[nb] & OPERATOR_SCHEMA_BASE_FACE) {
      for (int f = 0; f < nfaces_owned; f++) {
        mesh_->face_get_cells(f, AmanziMesh::USED, &cells);
        int ncells = cells.size();

        int nd;
        if (subschema == OPERATOR_SCHEMA_DOFS_CELL) {
          for (int n = 0; n < ncells; n++) {
            lid[n] = offset_my_[1] + cells[n];
            gid[n] = offset_global_[1] + cmap_wghost.GID(cells[n]);
          }
          nd = ncells;
        }

        for (int n = 0; n < nd; n++) {
          if (lid[n] < ndof) {
            ff_graph.InsertGlobalIndices(gid[n], nd, gid);
          } else {
            ff_graph_off.InsertMyIndices(lid[n] - ndof, nd, lid);
          }
        }
      }
    } else if (blocks_schema_[nb] & OPERATOR_SCHEMA_BASE_NODE) {
      // not implemented yet
    }
  }

  // Completing and optimizing the graphs
  ff_graph_off.FillComplete(*map, *map);

  // Exporter is used in matrix assembly. However, the graph  should 
  // be already complete for our applications. 
  exporter_ = Teuchos::rcp(new Epetra_Export(*map_off, *map));
  ff_graph.Export(ff_graph_off, *exporter_, Insert);
  
  // assemble the graphs
  ff_graph.FillComplete(*map, *map);

  delete map;
  delete map_wghost;
  delete map_off;

  // create global matrix
  A_ = Teuchos::rcp(new Epetra_CrsMatrix(Copy, ff_graph));
  A_off_ = Teuchos::rcp(new Epetra_CrsMatrix(Copy, ff_graph_off));
}
#endif


#ifdef OPERATORS_MATRIX_FE_CRS
/* ******************************************************************
* Assemble elemental face-based matrices into four global matrices. 
****************************************************************** */
void Operator::AssembleMatrix(int schema)
{
  const Epetra_Map& fmap_wghost = mesh_->face_map(true);
  const Epetra_Map& cmap_wghost = mesh_->cell_map(true);
  const Epetra_Map& vmap_wghost = mesh_->node_map(true);

  // populate matrix
  A_->PutScalar(0.0);

  AmanziMesh::Entity_ID_List cells, faces, nodes;
  int gid[OPERATOR_MAX_NODES + 1];

  int nblocks = blocks_.size();
  for (int nb = 0; nb < nblocks; nb++) {
    int subschema = blocks_schema_[nb] & schema;
    std::vector<WhetStone::DenseMatrix>& matrix = *blocks_[nb];

    if (blocks_schema_[nb] & OPERATOR_SCHEMA_BASE_CELL) {
      for (int c = 0; c < ncells_owned; c++) {
        if (subschema == OPERATOR_SCHEMA_DOFS_FACE + OPERATOR_SCHEMA_DOFS_CELL) {
          mesh_->cell_get_faces(c, &faces);
          int nfaces = faces.size();

          for (int n = 0; n < nfaces; n++) {
            gid[n] = fmap_wghost.GID(faces[n]);
          }
          gid[nfaces] = offset_global_[1] + cmap_wghost.GID(c);

          A_->SumIntoGlobalValues(nfaces + 1, gid, matrix[c].Values());

        } else if (subschema == OPERATOR_SCHEMA_DOFS_FACE) {
          mesh_->cell_get_faces(c, &faces);
          int nfaces = faces.size();

          for (int n = 0; n < nfaces; n++) {
            gid[n] = fmap_wghost.GID(faces[n]);
          }

          A_->SumIntoGlobalValues(nfaces, gid, matrix[c].Values());

        } else if (subschema == OPERATOR_SCHEMA_DOFS_NODE) {
          mesh_->cell_get_nodes(c, &nodes);
          int nnodes = nodes.size();

          for (int n = 0; n < nnodes; n++) {
            gid[n] = offset_global_[2] + vmap_wghost.GID(nodes[n]);
          }

          A_->SumIntoGlobalValues(nnodes, gid, matrix[c].Values());
        }
      }
    // new schema
    } else if (blocks_schema_[nb] & OPERATOR_SCHEMA_BASE_FACE) {
      for (int f = 0; f < nfaces_owned; f++) {
        if (subschema == OPERATOR_SCHEMA_DOFS_CELL) {
          mesh_->face_get_cells(f, AmanziMesh::USED, &cells);
          int ncells = cells.size();

          for (int n = 0; n < ncells; n++) {
            gid[n] = offset_global_[1] + cmap_wghost.GID(cells[n]);
          }

          A_->SumIntoGlobalValues(ncells, gid, matrix[f].Values());
        }
      }
    }
  }

  A_->GlobalAssemble();

  // Add diagonal (a hack)
  Epetra_Vector tmp(A_->RowMap());
  A_->ExtractDiagonalCopy(tmp);

  if (diagonal_->HasComponent("face")) {
    Epetra_MultiVector& diag = *diagonal_->ViewComponent("face");
    for (int f = 0; f < nfaces_owned; f++) tmp[f] += diag[0][f];
  }

  if (diagonal_->HasComponent("cell")) {
    Epetra_MultiVector& diag = *diagonal_->ViewComponent("cell");
    for (int c = 0; c < ncells_owned; c++) tmp[offset_my_[1] + c] += diag[0][c];
  }

  if (diagonal_->HasComponent("node")) {
    Epetra_MultiVector& diag = *diagonal_->ViewComponent("node");
    for (int v = 0; v < nnodes_owned; v++) tmp[offset_my_[2] + v] += diag[0][v];
  }

  A_->ReplaceDiagonalValues(tmp);
}


#else
/* ******************************************************************
* Assemble elemental face-based matrices into four global matrices. 
****************************************************************** */
void Operator::AssembleMatrix(int schema)
{
  const Epetra_Map& fmap_wghost = mesh_->face_map(true);
  const Epetra_Map& cmap_wghost = mesh_->cell_map(true);
  const Epetra_Map& vmap_wghost = mesh_->node_map(true);

  int ndof = A_->NumMyRows();

  // populate matrix
  A_->PutScalar(0.0);
  A_off_->PutScalar(0.0);

  AmanziMesh::Entity_ID_List cells, faces, nodes;
  int lid[OPERATOR_MAX_NODES + 1];
  int gid[OPERATOR_MAX_NODES + 1];
  double values[OPERATOR_MAX_NODES + 1];

  int nblocks = blocks_.size();
  for (int nb = 0; nb < nblocks; nb++) {
    int subschema = blocks_schema_[nb] & schema;
    std::vector<WhetStone::DenseMatrix>& matrix = *blocks_[nb];

    if (blocks_schema_[nb] & OPERATOR_SCHEMA_BASE_CELL) {
      for (int c = 0; c < ncells_owned; c++) {
        if (subschema == OPERATOR_SCHEMA_DOFS_FACE + OPERATOR_SCHEMA_DOFS_CELL) {
          mesh_->cell_get_faces(c, &faces);
          int nfaces = faces.size();

          for (int n = 0; n < nfaces; n++) {
            lid[n] = faces[n];
            if (faces[n] >= nfaces_owned) lid[n] += ndof - nfaces_owned; 
            gid[n] = fmap_wghost.GID(faces[n]);
          }
          lid[nfaces] = offset_my_[1] + c;
          gid[nfaces] = offset_global_[1] + cmap_wghost.GID(c);

          WhetStone::DenseMatrix& Acell = matrix[c];
          for (int n = 0; n < nfaces + 1; n++) {
            for (int m = 0; m < nfaces + 1; m++) values[m] = Acell(n, m);
            if (lid[n] < ndof) {
              A_->SumIntoMyValues(lid[n], nfaces + 1, values, lid);
            } else {
              A_off_->SumIntoMyValues(lid[n] - ndof, nfaces + 1, values, lid);
            }
          }

        } else if (subschema == OPERATOR_SCHEMA_DOFS_FACE) {
          mesh_->cell_get_faces(c, &faces);
          int nfaces = faces.size();

          for (int n = 0; n < nfaces; n++) {
            lid[n] = faces[n];
            gid[n] = fmap_wghost.GID(faces[n]);
          }

          WhetStone::DenseMatrix& Acell = matrix[c];
          for (int n = 0; n < nfaces; n++) {
            for (int m = 0; m < nfaces; m++) values[m] = Acell(n, m);
            if (lid[n] < ndof) {
              A_->SumIntoMyValues(lid[n], nfaces, values, lid);
            } else {
              A_off_->SumIntoMyValues(lid[n] - ndof, nfaces, values, lid);
            }
          }

        } else if (subschema == OPERATOR_SCHEMA_DOFS_NODE) {
          mesh_->cell_get_nodes(c, &nodes);
          int nnodes = nodes.size();

          for (int n = 0; n < nnodes; n++) {
            lid[n] = offset_my_[2] + nodes[n];
            gid[n] = offset_global_[2] + vmap_wghost.GID(nodes[n]);
          }

          WhetStone::DenseMatrix& Acell = matrix[c];
          for (int n = 0; n < nnodes; n++) {
            for (int m = 0; m < nnodes; m++) values[m] = Acell(n, m);
            if (lid[n] < ndof) {
              A_->SumIntoMyValues(lid[n], nnodes, values, lid);
            } else {
              A_off_->SumIntoMyValues(lid[n] - ndof, nnodes, values, lid);
            }
          }
        }
      }
    // new schema
    } else if (blocks_schema_[nb] & OPERATOR_SCHEMA_BASE_FACE) {
      for (int f = 0; f < nfaces_owned; f++) {
        if (subschema == OPERATOR_SCHEMA_DOFS_CELL) {
          mesh_->face_get_cells(f, AmanziMesh::USED, &cells);
          int ncells = cells.size();

          for (int n = 0; n < ncells; n++) {
            gid[n] = offset_global_[1] + cmap_wghost.GID(cells[n]);
          }

          WhetStone::DenseMatrix& Acell = matrix[f];
          for (int n = 0; n < ncells; n++) {
            for (int m = 0; m < ncells; m++) values[m] = Acell(n, m);
            A_->SumIntoMyValues(gid[n], ncells, values, gid);
          }
        }
      }
    }
  }

  // Scatter off proc to their locations
  A_->Export(*A_off_, *exporter_, Add);
  
  const Epetra_Map& map = A_->RowMap();
  A_->FillComplete(map, map);

  // Add diagonal (a hack)
  Epetra_Vector tmp(A_->RowMap());
  A_->ExtractDiagonalCopy(tmp);

  if (diagonal_->HasComponent("face")) {
    Epetra_MultiVector& diag = *diagonal_->ViewComponent("face");
    for (int f = 0; f < nfaces_owned; f++) tmp[f] += diag[0][f];
  }

  if (diagonal_->HasComponent("cell")) {
    Epetra_MultiVector& diag = *diagonal_->ViewComponent("cell");
    for (int c = 0; c < ncells_owned; c++) tmp[offset_my_[1] + c] += diag[0][c];
  }

  if (diagonal_->HasComponent("node")) {
    Epetra_MultiVector& diag = *diagonal_->ViewComponent("node");
    for (int v = 0; v < nnodes_owned; v++) tmp[offset_my_[2] + v] += diag[0][v];
  }

  A_->ReplaceDiagonalValues(tmp);
}
#endif


/* ******************************************************************
* Applies boundary conditions to matrix_blocks and update the
* right-hand side and the diagonal block.                                           
****************************************************************** */
void Operator::ApplyBCs(std::vector<int>& bc_model, std::vector<double>& bc_values)
{
  // clean ghosted values
  for (CompositeVector::name_iterator name = rhs_->begin(); name != rhs_->end(); ++name) {
    Epetra_MultiVector& rhs_g = *rhs_->ViewComponent(*name, true);
    int n0 = rhs_->ViewComponent(*name, false)->MyLength();
    int n1 = rhs_g.MyLength();
    for (int i = n0; i < n1; i++) rhs_g[0][i] = 0.0;

    Epetra_MultiVector& diag_g = *diagonal_->ViewComponent(*name, true);
    for (int i = n0; i < n1; i++) diag_g[0][i] = 0.0;
  }

  AmanziMesh::Entity_ID_List faces, nodes;

  int nblocks = blocks_.size();
  for (int nb = 0; nb < nblocks; nb++) {
    int schema = blocks_schema_[nb];
    std::vector<WhetStone::DenseMatrix>& matrix = *blocks_[nb];
    std::vector<WhetStone::DenseMatrix>& matrix_shadow = *blocks_shadow_[nb];

    if (schema & OPERATOR_SCHEMA_BASE_CELL) {
      if ((schema & OPERATOR_SCHEMA_DOFS_FACE) && (schema & OPERATOR_SCHEMA_DOFS_CELL)) {
        Epetra_MultiVector& rhs_face = *rhs_->ViewComponent("face", true);
        Epetra_MultiVector& rhs_cell = *rhs_->ViewComponent("cell");
        Epetra_MultiVector& diag = *diagonal_->ViewComponent("face");

        for (int c = 0; c < ncells_owned; c++) {
          mesh_->cell_get_faces(c, &faces);
          int nfaces = faces.size();

          WhetStone::DenseMatrix& Acell = matrix[c];

          bool flag(true);
          for (int n = 0; n < nfaces; n++) {
            int f = faces[n];
            double value = bc_values[f];

            if (bc_model[f] == OPERATOR_BC_FACE_DIRICHLET) {
              if (flag) {  // make a copy of elemntal matrix
                matrix_shadow[c] = Acell;
                flag = false;
              }
              for (int m = 0; m < nfaces; m++) {
                rhs_face[0][faces[m]] -= Acell(m, n) * value;
                Acell(n, m) = Acell(m, n) = 0.0;
              }
              rhs_face[0][f] = value;
              diag[0][f] = 1.0;

              rhs_cell[0][c] -= Acell(nfaces, n) * value;
              Acell(nfaces, n) = 0.0;
              Acell(n, nfaces) = 0.0;
            } else if (bc_model[f] == OPERATOR_BC_FACE_NEUMANN) {
              rhs_face[0][f] -= value * mesh_->face_area(f);
            }
          }
        }
      } else if (schema & OPERATOR_SCHEMA_DOFS_NODE) {
        Epetra_MultiVector& rhs_node = *rhs_->ViewComponent("node", true);
        Epetra_MultiVector& diag = *diagonal_->ViewComponent("node", true);

        for (int c = 0; c < ncells_owned; c++) {
          mesh_->cell_get_nodes(c, &nodes);
          int nnodes = nodes.size();

          WhetStone::DenseMatrix& Acell = matrix[c];

          bool flag(true);
          for (int n = 0; n < nnodes; n++) {
            int v = nodes[n];
            double value = bc_values[v];

            if (bc_model[v] == OPERATOR_BC_FACE_DIRICHLET) {
              if (flag) {  // make a copy of cell-based matrix
                matrix_shadow[c] = Acell;
                flag = false;
              }
              for (int m = 0; m < nnodes; m++) {
                rhs_node[0][nodes[m]] -= Acell(m, n) * value;
                Acell(n, m) = Acell(m, n) = 0.0;
              }
              rhs_node[0][v] = value;
              diag[0][v] = 1.0;
            }
          }
        }
      }
    }
  }

  // Account for the ghosted values
  diagonal_->GatherGhostedToMaster(Add);
  rhs_->GatherGhostedToMaster(Add);
}


/* ******************************************************************
* Parallel matvec product A * X.                                              
******************************************************************* */
int Operator::Apply(const CompositeVector& X, CompositeVector& Y) const
{
  // initialize ghost elements 
  X.ScatterMasterToGhosted();
  Y.PutScalarMasterAndGhosted(0.0);

  // Multiply by the diagonal block.
  Y.Multiply(1.0, *diagonal_, X, 0.0);

  // Multiply by the remaining matrix blocks.
  AmanziMesh::Entity_ID_List faces, cells, nodes;

  int nblocks = blocks_.size();
  for (int nb = 0; nb < nblocks; nb++) {
    std::vector<WhetStone::DenseMatrix>& matrix = *blocks_[nb];
    int schema = blocks_schema_[nb];

    if (schema & OPERATOR_SCHEMA_BASE_CELL) {
      if (schema & OPERATOR_SCHEMA_DOFS_FACE && schema & OPERATOR_SCHEMA_DOFS_CELL) {
        const Epetra_MultiVector& Xf = *X.ViewComponent("face", true);
        const Epetra_MultiVector& Xc = *X.ViewComponent("cell");

        Epetra_MultiVector& Yf = *Y.ViewComponent("face", true);
        Epetra_MultiVector& Yc = *Y.ViewComponent("cell");

        for (int c = 0; c < ncells_owned; c++) {
          mesh_->cell_get_faces(c, &faces);
          int nfaces = faces.size();

          WhetStone::DenseVector v(nfaces + 1), av(nfaces + 1);
          for (int n = 0; n < nfaces; n++) {
            v(n) = Xf[0][faces[n]];
          }
          v(nfaces) = Xc[0][c];

          WhetStone::DenseMatrix& Acell = matrix[c];
          Acell.Multiply(v, av, false);

          for (int n = 0; n < nfaces; n++) {
            Yf[0][faces[n]] += av(n);
          }
          Yc[0][c] += av(nfaces);
        } 
      } else if (schema & OPERATOR_SCHEMA_DOFS_NODE) {
        const Epetra_MultiVector& Xv = *X.ViewComponent("node", true);
        Epetra_MultiVector& Yv = *Y.ViewComponent("node", true);

        for (int c = 0; c < ncells_owned; c++) {
          mesh_->cell_get_nodes(c, &nodes);
          int nnodes = nodes.size();

          WhetStone::DenseVector v(nnodes), av(nnodes);
          for (int n = 0; n < nnodes; n++) {
            v(n) = Xv[0][nodes[n]];
          }

          WhetStone::DenseMatrix& Acell = matrix[c];
          Acell.Multiply(v, av, false);

          for (int n = 0; n < nnodes; n++) {
            Yv[0][nodes[n]] += av(n);
          }
        } 
      }
    // new schema
    } else if (schema & OPERATOR_SCHEMA_BASE_FACE) {
      if (schema & OPERATOR_SCHEMA_DOFS_CELL) {
        const Epetra_MultiVector& Xc = *X.ViewComponent("cell", true);
        Epetra_MultiVector& Yc = *Y.ViewComponent("cell", true);

        for (int f = 0; f < nfaces_owned; f++) {
          mesh_->face_get_cells(f, AmanziMesh::USED, &cells);
          int ncells = cells.size();

          WhetStone::DenseVector v(ncells), av(ncells);
          for (int n = 0; n < ncells; n++) {
            v(n) = Xc[0][cells[n]];
          }

          WhetStone::DenseMatrix& Aface = matrix[f];
          Aface.Multiply(v, av, false);

          for (int n = 0; n < ncells; n++) {
            Yc[0][cells[n]] += av(n);
          }
        }
      } 
    }
  }
  Y.GatherGhostedToMaster(Add);

  return 0;
}


/* ******************************************************************
* Parallel matvec product A * X.                                              
******************************************************************* */
int Operator::ApplyInverse(const CompositeVector& X, CompositeVector& Y) const
{
  // Y = X;
  // return 0;

  Epetra_Vector Xcopy(A_->RowMap());
  Epetra_Vector Ycopy(A_->RowMap());

  if (X.HasComponent("face")) {
    const Epetra_MultiVector& data = *X.ViewComponent("face");
    for (int f = 0; f < nfaces_owned; f++) Xcopy[f] = data[0][f];
  } 
  if (X.HasComponent("cell")) {
    const Epetra_MultiVector& data = *X.ViewComponent("cell");
    for (int c = 0; c < ncells_owned; c++) Xcopy[c + offset_my_[1]] = data[0][c];
  } 
  if (X.HasComponent("node")) {
    const Epetra_MultiVector& data = *X.ViewComponent("node");
    for (int v = 0; v < nnodes_owned; v++) Xcopy[v + offset_my_[2]] = data[0][v];
  } 

  int ierr = preconditioner_->ApplyInverse(Xcopy, Ycopy);

  if (Y.HasComponent("face")) {
    Epetra_MultiVector& data = *Y.ViewComponent("face");
    for (int f = 0; f < nfaces_owned; f++) data[0][f] = Ycopy[f];
  } 
  if (Y.HasComponent("cell")) {
    Epetra_MultiVector& data = *Y.ViewComponent("cell");
    for (int c = 0; c < ncells_owned; c++) data[0][c] = Ycopy[c + offset_my_[1]];
  } 
  if (Y.HasComponent("node")) {
    Epetra_MultiVector& data = *Y.ViewComponent("node");
    for (int v = 0; v < nnodes_owned; v++) data[0][v] = Ycopy[v + offset_my_[2]];
  } 

  return 0;
}


/* ******************************************************************
* Initialization of the preconditioner. Note that boundary conditions
* may be used in re-implementation of this virtual function.
****************************************************************** */
void Operator::InitPreconditioner(const std::string& prec_name, const Teuchos::ParameterList& plist,
                                  std::vector<int>& bc_model, std::vector<double>& bc_values)
{
  AmanziPreconditioners::PreconditionerFactory factory;
  preconditioner_ = factory.Create(prec_name, plist);
  preconditioner_->Update(A_);
}


/* ******************************************************************
* Adds time derivative ss * (u - u0) / dT.
****************************************************************** */
void Operator::AddAccumulationTerm(
    const CompositeVector& u0, const CompositeVector& ss, double dT)
{
  AmanziMesh::Entity_ID_List nodes;

  std::string name;
  CompositeVector entity_volume(ss);

  if (ss.HasComponent("cell")) {
    name = "cell";
    Epetra_MultiVector& volume = *entity_volume.ViewComponent(name); 

    for (int c = 0; c < ncells_owned; c++) {
      volume[0][c] = mesh_->cell_volume(c); 
    }
  } else if (ss.HasComponent("face")) {
    name = "face";
    // Missing code.
  } else if (ss.HasComponent("node")) {
    name = "node";
    Epetra_MultiVector& volume = *entity_volume.ViewComponent(name, true); 
    volume.PutScalar(0.0);

    for (int c = 0; c < ncells_owned; c++) {
      mesh_->cell_get_nodes(c, &nodes);
      int nnodes = nodes.size();

      for (int i = 0; i < nnodes; i++) {
        volume[0][nodes[i]] += mesh_->cell_volume(c) / nnodes; 
      }
    }

    entity_volume.GatherGhostedToMaster(name);
  }

  const Epetra_MultiVector& u0c = *u0.ViewComponent(name);
  const Epetra_MultiVector& ssc = *ss.ViewComponent(name);

  Epetra_MultiVector& volume = *entity_volume.ViewComponent(name); 
  Epetra_MultiVector& diag = *diagonal_->ViewComponent(name);
  Epetra_MultiVector& rhs = *rhs_->ViewComponent(name);

  int n = u0c.MyLength();
  for (int i = 0; i < n; i++) {
    double factor = volume[0][i] * ssc[0][i] / dT;
    diag[0][i] += factor;
    rhs[0][i] += factor * u0c[0][i];
  }
}


/* ******************************************************************
* Adds time derivative ss * (u - u0).
****************************************************************** */
void Operator::AddAccumulationTerm(const CompositeVector& u0, const CompositeVector& ss)
{
  std::string name;
  if (ss.HasComponent("cell")) {
    name = "cell";
  } else if (ss.HasComponent("face")) {
    name = "face";
    // Missing code.
  } else if (ss.HasComponent("node")) {
    name = "node";
  }

  const Epetra_MultiVector& u0c = *u0.ViewComponent(name);
  const Epetra_MultiVector& ssc = *ss.ViewComponent(name);

  Epetra_MultiVector& diag = *diagonal_->ViewComponent(name);
  Epetra_MultiVector& rhs = *rhs_->ViewComponent(name);

  int n = u0c.MyLength();
  for (int i = 0; i < n; i++) {
    diag[0][i] += ssc[0][i];
    rhs[0][i] += ssc[0][i] * u0c[0][i];
  }
}


/* ******************************************************************
* Check points allows us to revert boundary conditions, source terms, 
* and accumulation terms. They are useful for operators with constant
* coefficients and varying boundary conditions, e.g. for modeling
* saturated flows.
****************************************************************** */
void Operator::CreateCheckPoint()
{
  diagonal_checkpoint_ = Teuchos::rcp(new CompositeVector(*diagonal_));
  rhs_checkpoint_ = Teuchos::rcp(new CompositeVector(*rhs_));
}

void Operator::RestoreCheckPoint()
{
  // The routine should be called after checkpoint is created.
  ASSERT(diagonal_checkpoint_ != Teuchos::null);
  ASSERT(rhs_checkpoint_ != Teuchos::null);

  // restore accumulation and source terms
  *diagonal_ = *diagonal_checkpoint_;
  *rhs_ = *rhs_checkpoint_;

  // restore boundary conditions
  int n = blocks_.size();
  for (int i = 0; i < n; i++) { 
    std::vector<WhetStone::DenseMatrix>& matrix = *blocks_[i];
    std::vector<WhetStone::DenseMatrix>& matrix_shadow = *blocks_shadow_[i];
    int m = matrix.size();
    for (int k = 0; k < m; k++) {
      if (matrix_shadow[k].NumRows() != 0) {
        matrix[k] = matrix_shadow[k];
      }
    }
  }
}

}  // namespace Operators
}  // namespace Amanzi



