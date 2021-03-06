# -*- mode: cmake -*-

#
#  Amanzi
#    Amanzi executables
#    Directory contains the source code to produce stand-alone
#    binaries.
#

# Amanzi module, include files found in AMANZI_MODULE_PATH
include(PrintVariable)
include(TestManager)
include(BinaryManager)

#
# Define a project name
# After this command the following varaibles are defined
#   EXEC_SOURCE_DIR
#   EXEC_BINARY_DIR
# Other projects (subdirectories) can reference this directory
# through these variables.
project(EXEC)

# Amanzi include directories
include_directories(${ATK_SOURCE_DIR})
include_directories(${DATA_STRUCTURES_SOURCE_DIR})
include_directories(${DBC_SOURCE_DIR})
include_directories(${DBG_SOURCE_DIR})
include_directories(${GEOMETRY_SOURCE_DIR})
include_directories(${MESH_SOURCE_DIR})
include_directories(${MESH_DATA_SOURCE_DIR})
include_directories(${MESH_FACTORY_SOURCE_DIR})
include_directories(${OUTPUT_SOURCE_DIR})
include_directories(${STATE_SOURCE_DIR})
include_directories(${CHEM_SOURCE_DIR})
include_directories(${CHEMPK_SOURCE_DIR})
include_directories(${ENERGY_SOURCE_DIR})
include_directories(${FLOW_SOURCE_DIR})
include_directories(${TRANSPORT_SOURCE_DIR})
include_directories(${WHETSTONE_SOURCE_DIR})
include_directories(${OPERATORS_SOURCE_DIR})
include_directories(${MPC_SOURCE_DIR})
include_directories(${TIME_INTEGRATION_SOURCE_DIR})
include_directories(${FUNCS_SOURCE_DIR})
include_directories(${MFUNCS_SOURCE_DIR})
include_directories(${PLATFORM_INTERFACE_SOURCE_DIR})
include_directories(${SOLVERS_SOURCE_DIR})
include_directories(${EOS_SOURCE_DIR})
include_directories(${MPC_TREE_SOURCE_DIR})
include_directories(${PKS_SOURCE_DIR})

# Chemistry includes directory
get_property(CHEM_INCLUDES_DIR GLOBAL PROPERTY CHEM_INCLUDES_DIR)
include_directories(${CHEM_INCLUDES_DIR})

# The mesh frameworks
set(amanzi_mesh_libs "")
list(APPEND amanzi_mesh_libs stk_mesh exodus_reader)
if(ENABLE_STK_Mesh)
  add_definitions("-DHAVE_STK_MESH")
endif()
if(ENABLE_MOAB_Mesh)
  add_definitions("-DHAVE_MOAB_MESH")
endif()
if(ENABLE_MSTK_Mesh)
  add_definitions("-DHAVE_MSTK_MESH")
endif()

# XML ouput
if(ENABLE_NATIVE_XML_OUTPUT)
  add_definitions("-DENABLE_NATIVE_XML_OUTPUT")
endif()

# External (TPL) include directories
include_directories(${Boost_INCLUDE_DIRS})
include_directories(${Teuchos_INCLUDE_DIRS})
include_directories(${Epetra_INCLUDE_DIRS})
if(ENABLE_MOAB_Mesh)
    include_directories(${MOAB_INCLUDE_DIRS})
    include_directories(${MOAB_SOURCE_DIR})
    list(APPEND amanzi_mesh_libs moab_mesh)
endif()
if(ENABLE_STK_Mesh)
    include_directories(${STK_INCLUDE_DIRS})
    include_directories(${STK_SOURCE_DIR})
endif()
include_directories(${HDF5_C_INCLUDE_DIR})
include_directories(${ASCEMIO_INCLUDE_DIR})

# Copy input files to build directory if out of source build
 if (NOT (EXEC_SOURCE_DIR STREQUAL EXEC_BINARY_DIR))
        execute_process(COMMAND ${CMAKE_COMMAND} -E 
          copy_directory ${EXEC_SOURCE_DIR}/inputfiles ${EXEC_BINARY_DIR}/inputfiles ) 
    endif()

add_amanzi_library(udriv
                   SOURCE AmanziUnstructuredGridSimulationDriver.cc
                   LINK_LIBS 
                            ${Epetra_LIBRARIES} 
                            ${Teuchos_LIBRARIES}
                            mesh mesh_audit state mpc error_handling
			                ${Boost_LIBRARIES})

#
# Binary: verify_mesh
add_amanzi_executable(verify_mesh
                      SOURCE VerifyMesh.cc
                      LINK_LIBS mesh_factory mesh mesh_audit ${Boost_LIBRARIES} ${Epetra_LIBRARIES})
		    		    
#
# Binary: verify_hex
# verify_hex uses deprecated boost::filesystem
# methods needs to pass version information to the
# prepoccessor
add_amanzi_executable(verify_hex
                      SOURCE VerifyHex.cc
                      LINK_LIBS mesh_factory
                                mesh mesh_audit
                                output ${Boost_LIBRARIES} )
set_target_properties(verify_hex PROPERTIES
                      COMPILE_DEFINES "${Boost_FILESYSTEM_DEFINES}")

#
# Binary: verify_deform
#
if ( ENABLE_STK_Mesh )
  add_amanzi_executable(verify_deform
                        SOURCE VerifyDeform.cc
                        LINK_LIBS mesh_factory
                                  mesh
                                  ${amanzi_mesh_libs}
                                  output
                                  ${Epetra_LIBRARIES}
                                  ${Teuchos_LIBRARIES})
endif()                                

#
# Binary: concat
#
add_amanzi_executable(concat
                      SOURCE concat.cc)
                                
#
#
#
add_install_shellscript(splitexo.sh)

#
# Install include files
#
add_install_include_file(AmanziUnstructuredGridSimulationDriver.hh)
