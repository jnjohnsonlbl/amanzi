/*
  This is the Operator component of the Amanzi code.

  Copyright 2010-2013 held jointly by LANS/LANL, LBNL, and PNNL. 
  Amanzi is released under the three-clause BSD License. 
  The terms of use and "as is" disclaimer for this license are 
  provided in the top-level COPYRIGHT file.

  Authors: Ethan Coon (ecoon@lanl.gov)
*/

#ifndef AMANZI_OP_HH_
#define AMANZI_OP_HH_

#include "DenseMatrix.hh"
#include "BCs.hh"
#include "OperatorDefs.hh"

namespace Amanzi {

class CompositeVector;

namespace Operators {

class Operator;

class Op {
 public:
  Op(int schema_, std::string& schema_string_,
     const Teuchos::RCP<const AmanziMesh::Mesh> mesh) :
      schema(schema_),
      schema_string(schema_string_),
      mesh_(mesh)
  {}

  void Init() {
    if (vals.size() > 0) {
      for (int i = 0; i != vals.size(); ++i) {
        vals[i] = 0.;
        vals_shadow[i] = 0.;
      }
    }

    WhetStone::DenseMatrix null_mat;
    if (matrices.size() > 0) {
      for (int i = 0; i != matrices.size(); ++i) {
        matrices[i] = 0.;
        matrices_shadow[i] = null_mat;
      }
    }
  }
    
  
  virtual void RestoreCheckPoint() {
    for (int i = 0; i != matrices.size(); ++i) {
      if (matrices_shadow[i].NumRows() != 0) {
        matrices[i] = matrices_shadow[i];
      }
    }
    vals = vals_shadow;
  }


  virtual bool Matches(int match_schema, int matching_rule) {
    if (matching_rule == OPERATOR_SCHEMA_RULE_EXACT) {
      if ((match_schema & schema) == schema) return true;
    } else if (matching_rule == OPERATOR_SCHEMA_RULE_SUBSET) {
      if (match_schema & schema) return true;
    }
    return false;
  }

  virtual void ApplyMatrixFreeOp(const Operator* assembler,
          const CompositeVector& X, CompositeVector& Y) const = 0;

  virtual void SymbolicAssembleMatrixOp(const Operator* assembler,
          const SuperMap& map, GraphFE& graph,
          int my_block_row, int my_block_col) const = 0;

  virtual void AssembleMatrixOp(const Operator* assembler,
          const SuperMap& map, MatrixFE& mat,
          int my_block_row, int my_block_col) const = 0;

  virtual bool ApplyBC(BCs& bc,
                       const Teuchos::Ptr<CompositeVector>& rhs,                       
                       bool bc_previously_applied) = 0;
  

 public:
  int schema;
  std::string schema_string;

  std::vector<double> vals;
  std::vector<double> vals_shadow;  
  std::vector<WhetStone::DenseMatrix> matrices;
  std::vector<WhetStone::DenseMatrix> matrices_shadow;

 protected:
  Teuchos::RCP<const AmanziMesh::Mesh> mesh_;
};

}  // namespace Operators
}  // namespace Amanzi


#endif

