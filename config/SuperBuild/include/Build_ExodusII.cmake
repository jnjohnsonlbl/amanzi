#  -*- mode: cmake -*-

#
# Build TPL: ExodusII 
#   

# --- Define all the directories and common external project flags
define_external_project_args(ExodusII 
                             TARGET exodusii
                             DEPENDS NetCDF)

# --- Define the configure command

# Need to define variables used in the configure script
include(BuildWhitespaceString)
build_whitespace_string(common_cmake_args
                       ${Amanzi_CMAKE_C_COMPILER_ARGS}
		       -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE})

# Build the configure script
set(ExodusII_sh_configure ${ExodusII_prefix_dir}/exodusii-configure-step.sh)
configure_file(${SuperBuild_TEMPLATE_FILES_DIR}/exodusii-configure-step.sh.in
               ${ExodusII_sh_configure}
               @ONLY)

# Configure the CMake command file
set(ExodusII_cmake_configure ${ExodusII_prefix_dir}/exodusii-configure-step.cmake)
configure_file(${SuperBuild_TEMPLATE_FILES_DIR}/exodusii-configure-step.cmake.in
               ${ExodusII_cmake_configure}
               @ONLY)
set(ExodusII_CONFIGURE_COMMAND ${CMAKE_COMMAND} -P ${ExodusII_cmake_configure})  

# --- Add external project build and tie to the ZLIB build target
ExternalProject_Add(${ExodusII_BUILD_TARGET}
                    DEPENDS   ${ExodusII_PACKAGE_DEPENDS}             # Package dependency target
                    TMP_DIR   ${ExodusII_tmp_dir}                     # Temporary files directory
                    STAMP_DIR ${ExodusII_stamp_dir}                   # Timestamp and log directory
                    # -- Download and URL definitions
                    DOWNLOAD_DIR ${TPL_DOWNLOAD_DIR}                  # Download directory
                    URL          ${ExodusII_URL}                      # URL may be a web site OR a local file
                    URL_MD5      ${ExodusII_MD5_SUM}                  # md5sum of the archive file
                    # -- Configure
                    SOURCE_DIR         ${ExodusII_source_dir}         # Source directory
                    CONFIGURE_COMMAND  ${ExodusII_CONFIGURE_COMMAND}
                    # -- Build
                    BINARY_DIR        ${ExodusII_build_dir}           # Build directory 
		    BUILD_COMMAND     $(MAKE)
                    BUILD_IN_SOURCE   ${ExodusII_BUILD_IN_SOURCE}     # Flag for in source builds
                    # -- Install
                    INSTALL_DIR      ${TPL_INSTALL_PREFIX}            # Install directory
                    # -- Output control
                    ${ExodusII_logging_args})

# --- Add the nemsis build step		  

# Configure the build script
set(NEMESIS_sh_build ${ExodusII_prefix_dir}/nemesis-build.sh)
configure_file(${SuperBuild_TEMPLATE_FILES_DIR}/nemesis-build.sh.in
               ${NEMESIS_sh_build}
               @ONLY)

# Configure the CMake command file
set(NEMESIS_cmake_build ${ExodusII_prefix_dir}/nemesis-build.cmake)
configure_file(${SuperBuild_TEMPLATE_FILES_DIR}/nemesis-build.cmake.in
               ${NEMESIS_cmake_build}
               @ONLY)
set(NEMESIS_BUILD_COMMAND ${CMAKE_COMMAND} -P ${NEMESIS_cmake_build}) 

ExternalProject_Add_Step(${ExodusII_BUILD_TARGET} nemesis
                         COMMAND ${NEMESIS_BUILD_COMMAND}
                         COMMENT "Building nemesis (ExodusII extension)"
                         DEPENDEES install
                         WORKING_DIRECTORY ${ExodusII_prefix}
                         LOG TRUE)