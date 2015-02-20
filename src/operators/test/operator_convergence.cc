/*
  This is the operator component of the Amanzi code. 

  Copyright 2010-2012 held jointly by LANS/LANL, LBNL, and PNNL. 
  Amanzi is released under the three-clause BSD License. 
  The terms of use and "as is" disclaimer for this license are 
  provided in the top-level COPYRIGHT file.

  Author: Konstantin Lipnikov (lipnikov@lanl.gov)
*/

#include <cstdlib>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include "UnitTest++.h"

#include "Teuchos_RCP.hpp"
#include "Teuchos_ParameterList.hpp"
#include "Teuchos_ParameterXMLFileReader.hpp"

#include "MeshFactory.hh"
#include "GMVMesh.hh"
#include "LinearOperatorFactory.hh"

#include "tensor.hh"
#include "mfd3d_diffusion.hh"

#include "BCs.hh"
#include "OperatorDefs.hh"
#include "OperatorDiffusion.hh"

#include "Analytic01.hh"

/* *****************************************************************
* This test replaves tensor and boundary conditions by continuous
* functions. It is most sensitive to error in location of problem
* coefficients.
* **************************************************************** */
TEST(OPERATOR_MIXED_DIFFUSION) {
  using namespace Amanzi;
  using namespace Amanzi::AmanziMesh;
  using namespace Amanzi::AmanziGeometry;
  using namespace Amanzi::Operators;

  Epetra_MpiComm comm(MPI_COMM_WORLD);
  int MyPID = comm.MyPID();

  if (MyPID == 0) std::cout << "Test: 2D steady-state elliptic solver, mixed discretization" << std::endl;

  // read parameter list
  std::string xmlFileName = "test/operator_convergence.xml";
  Teuchos::ParameterXMLFileReader xmlreader(xmlFileName);
  Teuchos::ParameterList plist = xmlreader.getParameters();

  Amanzi::VerboseObject::hide_line_prefix = true;

  // create a mesh 
  Teuchos::ParameterList region_list = plist.get<Teuchos::ParameterList>("Regions");
  GeometricModelPtr gm = new GeometricModel(2, region_list, &comm);

  FrameworkPreference pref;
  pref.clear();
  pref.push_back(MSTK);

  MeshFactory meshfactory(&comm);
  meshfactory.preference(pref);
  // Teuchos::RCP<Mesh> mesh = meshfactory(0.0, 0.0, 1.0, 1.0, 40, 40, gm);
  Teuchos::RCP<const Mesh> mesh = meshfactory("test/median32x33.exo", gm);

  // create diffusion coefficient
  Teuchos::RCP<std::vector<WhetStone::Tensor> > K = Teuchos::rcp(new std::vector<WhetStone::Tensor>());
  int ncells = mesh->num_entities(AmanziMesh::CELL, AmanziMesh::OWNED);

  Analytic01 ana(mesh);

  for (int c = 0; c < ncells; c++) {
    const Point& xc = mesh->cell_centroid(c);
    const WhetStone::Tensor& Kc = ana.Tensor(xc, 0.0);
    K->push_back(Kc);
  }
  double rho(1.0), mu(1.0);

  // create boundary data
  int nfaces = mesh->num_entities(AmanziMesh::FACE, AmanziMesh::OWNED);
  int nfaces_wghost = mesh->num_entities(AmanziMesh::FACE, AmanziMesh::USED);
  Point xv(2);
  std::vector<int> bc_model(nfaces_wghost, Operators::OPERATOR_BC_NONE);
  std::vector<double> bc_value(nfaces_wghost);
  std::vector<double> bc_mixed;

  for (int f = 0; f < nfaces_wghost; f++) {
    const Point& xf = mesh->face_centroid(f);
    if (fabs(xf[0]) < 1e-6 || fabs(xf[0] - 1.0) < 1e-6 ||
        fabs(xf[1]) < 1e-6 || fabs(xf[1] - 1.0) < 1e-6) {
      bc_value[f] = ana.pressure_exact(xf, 0.0);
      bc_model[f] = Operators::OPERATOR_BC_DIRICHLET;
    }
  }
  Teuchos::RCP<BCs> bc = Teuchos::rcp(new BCs(Operators::OPERATOR_BC_TYPE_FACE, bc_model, bc_value, bc_mixed));

  // create diffusion operator 
  Teuchos::RCP<CompositeVectorSpace> cvs = Teuchos::rcp(new CompositeVectorSpace());
  cvs->SetMesh(mesh);
  cvs->SetGhosted(true);
  cvs->SetComponent("cell", AmanziMesh::CELL, 1);
  cvs->SetOwned(false);
  cvs->AddComponent("face", AmanziMesh::FACE, 1);

  CompositeVector solution(*cvs), flux(*cvs);

  // create source 
  CompositeVector source(*cvs);
  source.PutScalarMasterAndGhosted(0.0);
  
  Epetra_MultiVector& src = *source.ViewComponent("cell");
  for (int c = 0; c < ncells; c++) {
    const Point& xc = mesh->cell_centroid(c);
    src[0][c] += ana.source_exact(xc, 0.0);
  }

  // MAIN LOOP
  for (int n = 0; n < 240; n+=50) {
    double factor = pow(10.0, (double)(n - 50) / 100.0) / 2;
    
    // create the local diffusion operator
    Teuchos::ParameterList olist = plist.get<Teuchos::ParameterList>("PK operators")
                                        .get<Teuchos::ParameterList>("mixed diffusion");
    OperatorDiffusion op2(olist, mesh);

    int schema_dofs = op2.schema_dofs();
    int schema_prec_dofs = op2.schema_prec_dofs();
    CHECK(schema_dofs == (Operators::OPERATOR_SCHEMA_BASE_CELL
                          + Operators::OPERATOR_SCHEMA_DOFS_FACE
                          + Operators::OPERATOR_SCHEMA_DOFS_CELL));
    CHECK(schema_prec_dofs == (Operators::OPERATOR_SCHEMA_DOFS_FACE
            + Operators::OPERATOR_SCHEMA_DOFS_CELL));
            

    op2.set_factor(factor);  // for developers only
    op2.Setup(K, Teuchos::null, Teuchos::null, rho, mu);
    op2.UpdateMatrices(Teuchos::null, Teuchos::null);

    // get and assmeble the global operator
    Teuchos::RCP<Operator> global_op = op2.global_operator();
    global_op->UpdateRHS(source, false);
    global_op->ApplyBCs(bc);
    global_op->SymbolicAssembleMatrix();
    global_op->AssembleMatrix();
    
    Teuchos::ParameterList slist = plist.get<Teuchos::ParameterList>("Preconditioners");
    global_op->InitPreconditioner("Hypre AMG", slist);

    // solve the problem
    Teuchos::ParameterList lop_list = plist.get<Teuchos::ParameterList>("Solvers");
    solution.PutScalar(0.0);
    AmanziSolvers::LinearOperatorFactory<Operator, CompositeVector, CompositeVectorSpace> factory;
    Teuchos::RCP<AmanziSolvers::LinearOperator<Operator, CompositeVector, CompositeVectorSpace> >
       solver = factory.Create("AztecOO CG", lop_list, global_op);

    CompositeVector& rhs = *global_op->rhs();
    int ierr = solver->ApplyInverse(rhs, solution);

    // calculate pressure errors
    Epetra_MultiVector& p = *solution.ViewComponent("cell", false);
    double pnorm, pl2_err, pinf_err;
    ana.ComputeCellError(p, 0.0, pnorm, pl2_err, pinf_err);

    // calculate flux errors
    Epetra_MultiVector& flx = *flux.ViewComponent("face", true);
    double unorm, ul2_err, uinf_err;

    op2.UpdateFlux(solution, flux);
    flux.ScatterMasterToGhosted();
    ana.ComputeFaceError(flx, 0.0, unorm, ul2_err, uinf_err);

    if (MyPID == 0) {
      pl2_err /= pnorm;
      ul2_err /= unorm;
      printf("scale=%7.4g  L2(p)=%9.6f  Inf(p)=%9.6f  L2(u)=%9.6g  Inf(u)=%9.6f itr=%3d\n", 
          factor, pl2_err, pinf_err, ul2_err, uinf_err, solver->num_itrs()); 
    
      CHECK(pl2_err < 0.15 && ul2_err < 0.15);
    }
  }

  Epetra_MultiVector& p = *solution.ViewComponent("cell", false);
  GMV::open_data_file(*mesh, (std::string)"operators.gmv");
  GMV::start_data();
  GMV::write_cell_data(p, 0, "pressure");
  GMV::close_data_file();
}


/* *****************************************************************
* This test replaces tensor and boundary conditions by continuous
* functions. This is a prototype for future solvers.
* **************************************************************** */
/*
TEST(OPERATOR_NODAL_DIFFUSION) {
  using namespace Teuchos;
  using namespace Amanzi;
  using namespace Amanzi::AmanziMesh;
  using namespace Amanzi::AmanziGeometry;
  using namespace Amanzi::Operators;

  Epetra_MpiComm comm(MPI_COMM_WORLD);
  int MyPID = comm.MyPID();

  if (MyPID == 0) std::cout << "\nTest: 2D steady-state elliptic solver, nodal discretization" << std::endl;

  // read parameter list
  std::string xmlFileName = "test/operator_convergence.xml";
  ParameterXMLFileReader xmlreader(xmlFileName);
  ParameterList plist = xmlreader.getParameters();

  // create an SIMPLE mesh framework
  ParameterList region_list = plist.get<Teuchos::ParameterList>("Regions");
  GeometricModelPtr gm = new GeometricModel(2, region_list, &comm);

  FrameworkPreference pref;
  pref.clear();
  pref.push_back(MSTK);

  MeshFactory meshfactory(&comm);
  meshfactory.preference(pref);
  // RCP<Mesh> mesh = meshfactory(0.0, 0.0, 1.0, 1.0, 4, 4, gm);
  RCP<const Mesh> mesh = meshfactory("test/median32x33.exo", gm);
  // RCP<const Mesh> mesh = meshfactory("test/median255x256.exo", gm);

  // create diffusion coefficient
  Teuchos::RCP<std::vector<WhetStone::Tensor> > K = Teuchos::rcp(new std::vector<WhetStone::Tensor>());
  int ncells = mesh->num_entities(AmanziMesh::CELL, AmanziMesh::OWNED);

  Analytic01 ana(mesh);

  for (int c = 0; c < ncells_owned; c++) {
    const Point& xc = mesh->cell_centroid(c);
    const WhetStone::Tensor& Kc = ana.Tensor(xc, 0.0);
    K->push_back(Kc);
  }
  double rho(1.0), mu(1.0);

  // create boundary data (no mixed bc)
  Point xv(2);
  std::vector<int> bc_model(nnodes_wghost);
  std::vector<double> bc_value(nnodes_wghost);
  std::vector<double> bc_mixed;

  for (int v = 0; v < nnodes_wghost; v++) {
    mesh->node_get_coordinates(v, &xv);
    if (fabs(xv[0]) < 1e-6 || fabs(xv[0] - 1.0) < 1e-6 ||
        fabs(xv[1]) < 1e-6 || fabs(xv[1] - 1.0) < 1e-6) {
      bc_value[v] = ana.pressure_exact(xv, 0.0);
      bc_model[v] = Operators::OPERATOR_BC_DIRICHLET;
    }
  }
  Teuchos::RCP<BCs> bc = Teuchos::rcp(new BCs(Operators::OPERATOR_BC_TYPE_NODE, bc_model, bc_value, bc_mixed));

  // create diffusion operator 
  Teuchos::RCP<CompositeVectorSpace> cvs = Teuchos::rcp(new CompositeVectorSpace());
  cvs->SetMesh(mesh);
  cvs->SetGhosted(true);
  cvs->SetComponent("node", AmanziMesh::NODE, 1);

  CompositeVector solution(*cvs);
  solution.PutScalar(0.0);

  // create source 
  CompositeVector source(*cvs);
  source.PutScalarMasterAndGhosted(0.0);
  
  Epetra_MultiVector& src = *source.ViewComponent("node", true);
  for (int v = 0; v < nnodes_owned; v++) {
    mesh->node_get_coordinates(v, &xv);
    src[0][v] = ana.source_exact(xv, 0.0);
  }

  // MAIN LOOP
  for (int n = 0; n < 400; n+=110) {
    // double factor = pow(10.0, (double)(n - 50) / 100.0) / 2;
    double factor = pow(10.0, (double)(n - 150) / 100.0) / 2;

    // create source operator 
    Teuchos::RCP<OperatorSource> op1 = Teuchos::rcp(new OperatorSource(cvs, 0));
    op1->Init();
    op1->UpdateMatrices(source);

    // populate the diffusion operator
    Teuchos::ParameterList olist = plist.get<Teuchos::ParameterList>("PK operators")
                                        .get<Teuchos::ParameterList>("nodal diffusion");
    Teuchos::RCP<OperatorDiffusion> op2 = Teuchos::rcp(new OperatorDiffusion(*op1, olist, bc));
    int schema_dofs = op2->schema_dofs();
    CHECK(schema_dofs == Operators::OPERATOR_SCHEMA_DOFS_NODE);

    op2->set_factor(factor);  // for developers only
    op2->Setup(K, Teuchos::null, Teuchos::null, rho, mu);
    op2->UpdateMatrices(Teuchos::null, Teuchos::null);

    // get and assmeble the global operator
    Teuchos::RCP<Operator> global_op = op2->global_operator();
    global_op->ApplyBCs();
    global_op->SymbolicAssembleMatrix(schema_dofs);
    global_op->AssembleMatrix(schema_dofs);

    ParameterList slist = plist.get<Teuchos::ParameterList>("Preconditioners");
    global_op->InitPreconditioner("Hypre AMG", slist);

    // solve the problem
    ParameterList lop_list = plist.get<Teuchos::ParameterList>("Solvers");
    solution.PutScalar(0.0);
    AmanziSolvers::LinearOperatorFactory<Operator, CompositeVector, CompositeVectorSpace> factory;
    Teuchos::RCP<AmanziSolvers::LinearOperator<Operator, CompositeVector, CompositeVectorSpace> >
       solver = factory.Create("AztecOO CG", lop_list, global_op);

    CompositeVector& rhs = *global_op->rhs();
    solution.PutScalar(0.0);
    int ierr = solver->ApplyInverse(rhs, solution);
    CHECK(!ierr);

    // calculate errors
#ifdef HAVE_MPI
    solution.ScatterMasterToGhosted();
#endif

    Epetra_MultiVector& sol = *solution.ViewComponent("node", true);
    double pnorm, pl2_err, pinf_err, hnorm, ph1_err;
    ana.ComputeNodeError(sol, 0.0, pnorm, pl2_err, pinf_err, hnorm, ph1_err);

    if (MyPID == 0) {
      pl2_err /= pnorm;
      ph1_err /= hnorm;
      double tmp = op2->nfailed_primary() * 100.0 / ncells_owned; 
      printf("scale=%7.4g  L2(p)=%9.6f  Inf(p)=%9.6f  H1(p)=%9.6g  itr=%3d  nfailed=%4.1f\n", 
          factor, pl2_err, pinf_err, ph1_err, solver->num_itrs(), tmp); 

      CHECK(pl2_err < 0.1 && ph1_err < 0.15);
    }
  }
}
*/

