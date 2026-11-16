# Generate the bundled SDL car archives once for installation and browser embedding.
find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
set(restunts_car_archive_builder "${CMAKE_SOURCE_DIR}/tools/scripts/prepare-car-archives.py")
set(restunts_car_archive_directory "${CMAKE_CURRENT_BINARY_DIR}/car-archives")
set(restunts_car_archives TERR.zip MCMU.zip)
set(restunts_car_resource_files CARMCMU.RES CARTERR.RES STMCMU.3SH STMCMU.H3D STTERR.3SH
    STDAMCMU.VSH STDATERR.VSH STDBMCMU.VSH STDBTERR.VSH)
set(restunts_car_archive_inputs "${restunts_car_archive_builder}")
foreach(car_file IN LISTS restunts_car_resource_files)
    list(APPEND restunts_car_archive_inputs "${CMAKE_SOURCE_DIR}/assets/cars/${car_file}")
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${restunts_car_archive_inputs})
execute_process(COMMAND "${Python3_EXECUTABLE}" "${restunts_car_archive_builder}"
    --source-dir "${CMAKE_SOURCE_DIR}/assets/cars" --output-dir "${restunts_car_archive_directory}"
    RESULT_VARIABLE restunts_car_archive_result
    ERROR_VARIABLE restunts_car_archive_error)
if(NOT restunts_car_archive_result EQUAL 0)
    message(FATAL_ERROR "Unable to package the bundled cars: ${restunts_car_archive_error}")
endif()
