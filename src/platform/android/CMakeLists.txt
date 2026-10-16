# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

set(ANDROID_JNI_SRC
	${CMAKE_CURRENT_SOURCE_DIR}/jni_bridge.c
)

add_library(mgba-android SHARED ${ANDROID_JNI_SRC})
set_target_properties(mgba-android PROPERTIES
	OUTPUT_NAME mgba-android
	POSITION_INDEPENDENT_CODE ON
)
target_include_directories(mgba-android PRIVATE
	${PROJECT_SOURCE_DIR}/include
	${PROJECT_BINARY_DIR}/include
)
target_compile_definitions(mgba-android PRIVATE
	MGBA_ANDROID
	ENABLE_VFS
	ENABLE_DIRECTORIES
	ENABLE_VFS_FD
)
target_link_libraries(mgba-android PRIVATE mgba log)

