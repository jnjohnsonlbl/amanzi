# -*- mode: cmake -*-

#
#  Amanzi
#    Mulitprocess Coordinator (MPC)
#

# Amanzi module, include files found in AMANZI_MODULE_PATH
include(PrintVariable)
include(TestManager)
include(LibraryManager)

#
# Define a project name
# After this command the following varaibles are defined
#   MPC_SOURCE_DIR
#   MPC_BINARY_DIR
# Other projects (subdirectories) can reference this directory
# through these variables.
project(MPC)

# Amanzi include directories
include_directories(${DBC_SOURCE_DIR})
include_directories(${ATK_SOURCE_DIR})
include_directories(${GEOMETRY_SOURCE_DIR})
include_directories(${MESH_SOURCE_DIR})
include_directories(${MESH_DATA_SOURCE_DIR})
include_directories(${OUTPUT_SOURCE_DIR})
include_directories(${DATA_STRUCTURES_SOURCE_DIR})
include_directories(${STATE_SOURCE_DIR})
include_directories(${CHEM_SOURCE_DIR})
include_directories(${CHEMPK_SOURCE_DIR})
include_directories(${EOS_SOURCE_DIR})
include_directories(${TRANSPORT_SOURCE_DIR})
include_directories(${WHETSTONE_SOURCE_DIR})
include_directories(${FLOW_SOURCE_DIR})
include_directories(${DUMMYPK_SOURCE_DIR})
include_directories(${ENERGY_SOURCE_DIR})
include_directories(${TIME_INTEGRATION_SOURCE_DIR})
include_directories(${SOLVERS_SOURCE_DIR})
include_directories(${FUNCS_SOURCE_DIR})
include_directories(${MFUNCS_SOURCE_DIR})
include_directories(${DBG_SOURCE_DIR})
include_directories(${OPERATORS_SOURCE_DIR})
include_directories(${PKS_SOURCE_DIR})
include_directories(${MULTIPHASE_SOURCE_DIR})
include_directories(${MPC_TREE_SOURCE_DIR})


# Chemistry includes directory
get_property(CHEM_INCLUDES_DIR GLOBAL PROPERTY CHEM_INCLUDES_DIR)
include_directories(${CHEM_INCLUDES_DIR})

# External (TPL) include directories
include_directories(${Boost_INCLUDE_DIRS})
include_directories(${Teuchos_INCLUDE_DIRS})
include_directories(${Epetra_INCLUDE_DIRS})
include_directories(${HDF5_C_INCLUDE_DIR})
include_directories(${ASCEMIO_INCLUDE_DIR})

#
# Library: mpc
#
set(boost_libs_used ${Boost_FILESYSTEM_LIBRARY} ${Boost_SYSTEM_LIBRARY} ${Boost_REGEX_LIBRARY})
set(mpc_tpl_libs ${boost_libs_used} ${Teuchos_LIBRARIES} ${Epetra_LIBRARIES} ) 
list(REVERSE mpc_tpl_libs)
list(REMOVE_DUPLICATES mpc_tpl_libs) # Concerned about command line length! --lpritch
list(REVERSE mpc_tpl_libs)

set(mpc_inc_files
  CycleDriver.hh
  FlexibleObservations.hh
  WalkaboutCheckpoint.hh
  MeshInfo.hh
  Observable.hh
  ObservableFactory.hh
  ObservableAqueous.hh 
  ObservableLineSegment.hh
  ObservableSolute.hh
  ObservableLineSegmentAqueous.hh
  ObservableLineSegmentSolute.hh
)

add_amanzi_library(mpc 
                   SOURCE CycleDriver.cc 
                          FlexibleObservations.cc WalkaboutCheckpoint.cc 
                          ObservableFactory.cc MeshInfo.cc
                          ObservableAqueous.cc ObservableSolute.cc
                   HEADERS ${mpc_inc_files}
                   LINK_LIBS time_integration solvers output mpc_tree state mesh dbg ${mpc_tpl_libs})

if (BUILD_TESTS)
    include_directories(${MESH_FACTORY_SOURCE_DIR})
    
    # Add UnitTest includes
    include_directories(${UnitTest_INCLUDE_DIRS})
    # Add MPC include directory. Need to remove this required path. -- lpritch
    include_directories(${MPC_SOURCE_DIR})

    # Copy test subdirectory for out of source builds
    if (NOT (MPC_SOURCE_DIR STREQUAL MPC_BINARY_DIR))
        execute_process(COMMAND ${CMAKE_COMMAND} -E 
          copy_directory ${MPC_SOURCE_DIR}/test ${MPC_BINARY_DIR}/test) 
    endif()

    set(amanzi_libs mpc pks mpc_tree flow transport chemistry_pk energy dummy pks eos 
                    state operators mesh_factory 
                    geometry whetstone mesh_functions functions)
    
    # Test: mpc driver for flow
    add_amanzi_test(mpc_driver_flow mpc_driver_flow
                    KIND      int
                    SOURCE    test/Main.cc test/mpc_driver_flow.cc
		    LINK_LIBS ${amanzi_libs} ${UnitTest_LIBRARIES})

    # Test: mpc driver for transport
    add_amanzi_test(mpc_driver_transport mpc_driver_transport
                    KIND      int
                    SOURCE    test/Main.cc test/mpc_driver_transport.cc
		    LINK_LIBS ${amanzi_libs} ${UnitTest_LIBRARIES})

    # Test: mpc driver for reactive transport
    add_amanzi_test(mpc_driver_reactive_transport mpc_driver_reactive_transport
                    KIND      int
                    SOURCE    test/Main.cc test/mpc_driver_reactive_transport.cc
		    LINK_LIBS ${amanzi_libs} ${UnitTest_LIBRARIES})

    # Test: mpc driver for thermal richards
    add_amanzi_test(mpc_driver_thermal_richards mpc_driver_thermal_richards
                    KIND      int
                    SOURCE    test/Main.cc test/mpc_driver_thermal_richards.cc
		    LINK_LIBS ${amanzi_libs} ${UnitTest_LIBRARIES})

    # Test: mpc-new driver richards reactive transport
    add_amanzi_test(mpc_driver_flow_reactive_transport mpc_driver_flow_reactive_transport
                    KIND      int
                    SOURCE    test/Main.cc test/mpc_driver_frt.cc
		    LINK_LIBS ${amanzi_libs} ${UnitTest_LIBRARIES})

    # Test: mpc-new driver dummy
    add_amanzi_test(mpc_driver_dummy mpc_driver_dummy
                    KIND      int
                    SOURCE    test/Main.cc test/mpc_driver_dummy_pk.cc
                    LINK_LIBS ${amanzi_libs} ${UnitTest_LIBRARIES})

    # Test: observable_line_segment
    add_amanzi_test(observable_line_segment observable_line_segment
                    KIND      int
                    SOURCE    test/Main.cc test/observable_line_segment.cc
                    LINK_LIBS ${amanzi_libs} ${UnitTest_LIBRARIES})

#    add_executable(test_vis test/Main.cc test/test_vis.cpp)
#    target_link_libraries(test_vis state mpc stk_mesh ${UnitTest_LIBRARIES})
#    add_amanzi_test(vis test_vis KIND int)

endif()
