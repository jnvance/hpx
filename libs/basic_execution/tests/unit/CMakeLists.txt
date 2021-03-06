# Copyright (c) 2019 Thomas Heller
#
# SPDX-License-Identifier: BSL-1.0
# Distributed under the Boost Software License, Version 1.0. (See accompanying
# file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

set(tests
    execution_context
   )

foreach(test ${tests})
  set(sources
      ${test}.cpp)

  source_group("Source Files" FILES ${sources})

  # add example executable
  add_hpx_executable(${test}_test
                     SOURCES ${sources}
                     NOLIBS
                     DEPENDENCIES
                       hpx_basic_execution hpx_testing
                       ${BOOST_UNDERLYING_THREAD_LIBRARY}
                     EXCLUDE_FROM_ALL
                     FOLDER "Tests/Unit/Modules/BasicExecution")

  add_hpx_unit_test("modules.basic_execution" ${test} ${${test}_PARAMETERS})
  target_compile_definitions(${test}_test PRIVATE -DHPX_MODULE_STATIC_LINKING)

endforeach()
