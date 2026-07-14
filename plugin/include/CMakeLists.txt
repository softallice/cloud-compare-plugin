target_sources( ${PROJECT_NAME}
	PRIVATE
		${CMAKE_CURRENT_LIST_DIR}/qBuildingDims.h
		${CMAKE_CURRENT_LIST_DIR}/qBuildingDimsCommand.h
		${CMAKE_CURRENT_LIST_DIR}/BuildingDims.h
)

target_include_directories( ${PROJECT_NAME}
	PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}
)
