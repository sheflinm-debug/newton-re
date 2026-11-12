# the serial chip registry and the host's serial port over a TCP socket
# the cellular engine's parsing (host/HostCellular.h)
add_executable(test_HostCellular test_HostCellular.cpp)
target_link_libraries(test_HostCellular PRIVATE hal_host_cellular)
add_test(NAME hal.Cellular COMMAND test_HostCellular)

# the GPS receiver (host/HostGPS.h): its sentences and their parsing, a
# fixed receiver and a Tripmate run, and a log replayed
add_executable(test_HostGPS test_HostGPS.cpp)
target_link_libraries(test_HostGPS PRIVATE hal_host_gps)
add_test(NAME hal.GPS COMMAND test_HostGPS)
add_test(NAME hal.GPSReplay COMMAND test_HostGPS replay WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(hal.GPS hal.GPSReplay PROPERTIES TIMEOUT 60 PASS_REGULAR_EXPRESSION "all passed")

# the host's picture decoder (host/HostImage.h) against Pillow's decoding of
# the same pictures (pictures/, made by tools/host/picturefixtures.py)
add_executable(test_HostImage test_HostImage.cpp)
target_link_libraries(test_HostImage PRIVATE hal_host_image)
add_test(NAME hal.HostImage COMMAND test_HostImage ${CMAKE_CURRENT_SOURCE_DIR}/pictures)
set_tests_properties(hal.HostImage PROPERTIES TIMEOUT 60 PASS_REGULAR_EXPRESSION "all passed")

add_executable(test_HostSerialChip test_HostSerialChip.cpp)
target_link_libraries(test_HostSerialChip PRIVATE hal_host_serial os600_kernel os600_user)
add_test(NAME hal.HostSerialChip COMMAND test_HostSerialChip)
set_tests_properties(hal.HostSerialChip PROPERTIES TIMEOUT 60)

add_executable(test_FIQTimer test_FIQTimer.cpp)
target_link_libraries(test_FIQTimer PRIVATE hal_timers os600_kernel os600_user)
add_test(NAME hal.FIQTimer COMMAND test_FIQTimer)
set_tests_properties(hal.FIQTimer PROPERTIES TIMEOUT 60)

# the host's IR port: two chips over a socket, the modulation and half duplex
add_executable(test_HostIRChip test_HostIRChip.cpp)
target_link_libraries(test_HostIRChip PRIVATE hal_host_serial os600_kernel os600_user)
add_test(NAME hal.HostIRChip COMMAND test_HostIRChip)
set_tests_properties(hal.HostIRChip PROPERTIES TIMEOUT 60)

# the host's PC card and socket: the image, the CIS through the attribute
# window, the flash rules, the card-detect interrupt
add_executable(test_HostCard test_HostCard.cpp)
target_link_libraries(test_HostCard PRIVATE hal_host_card os600_kernel os600_user)
add_test(NAME hal.HostCard COMMAND test_HostCard)
set_tests_properties(hal.HostCard PROPERTIES TIMEOUT 60)

# the bank control register: its lane settings, and its error for a lane set
# it has none for (a ROM bug fixed: the error's sign)
add_executable(test_BankControl test_BankControl.cpp)
target_link_libraries(test_BankControl PRIVATE hal_host)
add_test(NAME hal.BankControl COMMAND test_BankControl)

# the mesh engine (host/HostMesh.h): two meshpeers on one pretend air - one
# hears the other's advert, sends it a message, is acknowledged and gets the
# echo back (tools/mesh/meshrun.py picks the air's port)
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
    add_test(NAME hal.MeshPeers
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/mesh/meshrun.py --seconds 60
                     --peer "$<TARGET_FILE:meshpeer> --air lan:{port} --name Echo --echo --seconds 60"
                     -- $<TARGET_FILE:meshpeer> --air lan:{port} --name Asker --send-to Echo --text hello --until "echo: hello" --seconds 50)
    set_tests_properties(hal.MeshPeers PROPERTIES TIMEOUT 90
                         PASS_REGULAR_EXPRESSION "sent delivered.*from Echo: echo: hello|from Echo: echo: hello.*sent delivered")
endif()
