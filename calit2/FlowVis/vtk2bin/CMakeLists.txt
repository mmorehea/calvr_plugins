SET(OUTPUT_BINDIR .)

ADD_EXECUTABLE(vtk2bin vtk2bin.cpp)

FIND_PACKAGE(FX)

IF(FX_FOUND)
    ADD_DEFINITIONS(-DWITH_FX_LIB)
    INCLUDE_DIRECTORIES(${FX_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(vtk2bin ${FX_LIBRARY} -lgfortran)
ENDIF(FX_FOUND)

INCLUDE_DIRECTORIES(${OSG_INCLUDE_DIR})
TARGET_LINK_LIBRARIES(vtk2bin ${OSG_LIBRARIES})
