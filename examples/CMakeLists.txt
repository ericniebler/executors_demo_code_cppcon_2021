# Copyright (c) 2019-present, Facebook, Inc.
#
# This source code is licensed under the license found in the
# LICENSE.txt file in the root directory of this source tree.

add_executable(example_1 example_1.cpp)
target_link_libraries(example_1 PUBLIC unifex)

add_executable(example_2 example_2.cpp)
target_link_libraries(example_2 PUBLIC unifex)

add_executable(example_3 example_3.cpp)
target_link_libraries(example_3 PUBLIC unifex)

add_executable(example_4 example_4.cpp)
target_link_libraries(example_4 PUBLIC unifex)
