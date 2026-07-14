target_sources( ${PROJECT_NAME}
	PRIVATE
		${CMAKE_CURRENT_LIST_DIR}/qBuildingDims.h
		${CMAKE_CURRENT_LIST_DIR}/qBuildingDimsCommand.h
		${CMAKE_CURRENT_LIST_DIR}/BuildingDims.h
		${CMAKE_CURRENT_LIST_DIR}/Planes.h
		${CMAKE_CURRENT_LIST_DIR}/Drawings.h
		${CMAKE_CURRENT_LIST_DIR}/DxfWriter.h
)

target_include_directories( ${PROJECT_NAME}
	PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}
)
