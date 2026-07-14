# qBuildingDims — CloudCompare Standard plugin
# Drop this directory into CloudCompare/plugins/core/Standard/qBuildingDims
# then enable PLUGIN_STANDARD_QBUILDINGDIMS in CMake.

option( PLUGIN_STANDARD_QBUILDINGDIMS "Install qBuildingDims plugin" OFF )

if ( PLUGIN_STANDARD_QBUILDINGDIMS )
	project( QBuildingDims )

	AddPlugin( NAME ${PROJECT_NAME} )

	add_subdirectory( include )
	add_subdirectory( src )

	# FileIOFilter / DXF export live in qCC_io.
	target_link_libraries( ${PROJECT_NAME} QCC_IO_LIB )
endif()
