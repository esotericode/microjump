# The actual executable owns every load, pickup, portal crossing, reset and save.
# Each subprocess uses an explicit disposable profile; never open a user's profile.
foreach(required GAME ROOT LEVELS REPLAY)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "Progress fixture needs -D${required}=...")
    endif()
endforeach()
file(MAKE_DIRECTORY "${ROOT}")
set(profile "${ROOT}/saved expedition.json")
set(packaged_profile "${ROOT}/packaged expedition.json")
set(corrupt_profile "${ROOT}/broken expedition.json")
file(REMOVE "${profile}" "${packaged_profile}" "${corrupt_profile}")
set(first "${ROOT}/fixture-a.json")
set(second "${ROOT}/fixture-b.json")

# A single straight run visits both places. An off-route spark in each place must
# remain missing; startup inspections run zero ticks, so cannot reacquire a find.
file(WRITE "${first}" [=[{
  "version":1, "name":"progress-a", "title":"First crossing",
  "shapes":[{"kind":"box","center":[0,-0.5,-25],"size":[30,1,80],"color":"#80a090"}],
  "checkpoints":[{"position":[0,0.01,0]},{"position":[0,0.01,-3]}],
  "collectibles":[{"name":"First light","position":[0,1,-1.8]},
                  {"name":"First unvisited light","position":[10,1,0]}],
  "portals":[{"name":"Across the crossing","position":[0,1,-6],"radius":0.6,
              "level":"fixture-b.json"}]
}]=])
file(WRITE "${second}" [=[{
  "version":1, "name":"progress-b", "title":"Second crossing",
  "shapes":[{"kind":"box","center":[0,-0.5,-25],"size":[30,1,80],"color":"#9080a0"}],
  "checkpoints":[{"position":[0,0.01,0]},{"position":[0,0.01,-3]}],
  "collectibles":[{"name":"Second light","position":[0,1,-1.8]},
                  {"name":"Second unvisited light","position":[10,1,0]}],
  "portals":[{"name":"Back across the crossing","position":[10,1,3],"radius":0.6,
              "level":"fixture-a.json","checkpoint":1}]
}]=])

function(run_game phase expected)
    execute_process(COMMAND "${GAME}" ${ARGN}
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 45)
    string(APPEND output "${errors}")
    message("Progress fixture ${phase}:\n${output}")
    if(NOT result STREQUAL "0")
        message(FATAL_ERROR "Progress fixture ${phase} failed: ${result}")
    endif()
    if(NOT output MATCHES "${expected}" OR
       NOT output MATCHES "Shutdown complete; Vulkan validation errors: 0")
        message(FATAL_ERROR "Progress fixture ${phase} missed its expected state or clean Vulkan shutdown")
    endif()
    set(last_output "${output}" PARENT_SCOPE)
endfunction()

function(require_unchanged path before phase)
    file(READ "${path}" after)
    if(NOT "${after}" STREQUAL "${before}")
        message(FATAL_ERROR "${phase} changed an isolated expedition profile")
    endif()
endfunction()

function(require_saved_level name expected_z expected_spark)
    file(READ "${profile}" saved)
    string(JSON count LENGTH "${saved}" levels)
    set(found FALSE)
    if(count GREATER 0)
        math(EXPR last "${count} - 1")
        foreach(index RANGE 0 ${last})
            string(JSON saved_name GET "${saved}" levels ${index} name)
            if(saved_name STREQUAL name)
                set(found TRUE)
                string(JSON saved_z GET "${saved}" levels ${index} checkpoint 2)
                string(JSON spark_count LENGTH "${saved}" levels ${index} sparks)
                if(NOT saved_z EQUAL expected_z OR NOT spark_count EQUAL 1)
                    message(FATAL_ERROR "Saved ${name} did not retain its checkpoint and one find")
                endif()
                string(JSON spark GET "${saved}" levels ${index} sparks 0)
                if(NOT spark STREQUAL expected_spark)
                    message(FATAL_ERROR "Saved ${name} exchanged its found and unvisited sparks")
                endif()
            endif()
        endforeach()
    endif()
    if(NOT found)
        message(FATAL_ERROR "Saved expedition has no progress for ${name}")
    endif()
endfunction()

run_game(collect
    "Expedition end: level progress-b, checkpoint 1, sparks 1/2"
    --save "${profile}" --level "${first}" --progress-test collect)
if(NOT last_output MATCHES "Traveled through 'Across the crossing' to progress-b")
    message(FATAL_ERROR "The expedition fixture did not naturally cross its gate")
endif()
if(NOT last_output MATCHES "Progress saved")
    message(FATAL_ERROR "The expedition fixture did not report its save")
endif()
if(NOT EXISTS "${profile}")
    message(FATAL_ERROR "The expedition fixture did not create its explicit profile")
endif()
require_saved_level(progress-a -3 "First light")
require_saved_level(progress-b -3 "Second light")

run_game(inspect_first
    "Expedition start: level progress-a, checkpoint 1, sparks 1/2"
    --save "${profile}" --level "${first}" --progress-test inspect)
run_game(inspect_second
    "Expedition start: level progress-b, checkpoint 1, sparks 1/2"
    --save "${profile}" --level "${second}" --progress-test inspect)

# Updating the authored arrays must not exchange the found and unfound sparks,
# nor change the safe checkpoint merely because its index changed.
file(READ "${second}" second_text)
string(REPLACE
    "{\"position\":[0,0.01,0]},{\"position\":[0,0.01,-3]}"
    "{\"position\":[0,0.01,-3]},{\"position\":[0,0.01,0]}"
    second_text "${second_text}")
string(REPLACE
    "{\"name\":\"Second light\",\"position\":[0,1,-1.8]},\n                  {\"name\":\"Second unvisited light\",\"position\":[10,1,0]}"
    "{\"name\":\"Second unvisited light\",\"position\":[10,1,0]},\n                  {\"name\":\"Second light\",\"position\":[0,1,-1.8]}"
    second_text "${second_text}")
file(WRITE "${second}" "${second_text}")
run_game(reordered_level
    "Expedition start: level progress-b, checkpoint 0, sparks 1/2"
    --save "${profile}" --level "${second}" --progress-test inspect)
require_saved_level(progress-b -3 "Second light")

# Only a packaged launch follows the last visited filename. Use authored data to
# keep this assertion about resuming a safe place, independent of level revisions.
file(READ "${LEVELS}/lantern-isles.json" packaged_level)
string(JSON packaged_name GET "${packaged_level}" name)
string(JSON packaged_checkpoint GET "${packaged_level}" checkpoints 2 position)
string(JSON packaged_spark GET "${packaged_level}" collectibles 0 name)
string(JSON packaged_count LENGTH "${packaged_level}" collectibles)
file(WRITE "${packaged_profile}"
    "{\"version\":1,\"last_level\":\"lantern-isles.json\",\"levels\":[{\"name\":\"${packaged_name}\",\"checkpoint\":${packaged_checkpoint},\"sparks\":[\"${packaged_spark}\"]}]}")
run_game(last_packaged_world
    "Expedition start: level ${packaged_name}, checkpoint 2, sparks 1/${packaged_count}"
    --save "${packaged_profile}" --progress-test inspect)
file(WRITE "${packaged_profile}"
    "{\"version\":1,\"last_level\":\"missing-expedition-place.json\",\"levels\":[]}")
file(READ "${LEVELS}/playground.json" playground_level)
string(JSON playground_name GET "${playground_level}" name)
string(JSON playground_count LENGTH "${playground_level}" collectibles)
run_game(missing_packaged_world
    "Expedition start: level ${playground_name}, checkpoint 0, sparks 0/${playground_count}"
    --save "${packaged_profile}" --progress-test inspect)

file(READ "${profile}" isolated_bytes)
run_game(isolated_replay
    "Replay finished: tick 0,.*checkpoint 0, sparks 0/2"
    --save "${profile}" --level "${first}" --replay "${REPLAY}")
require_unchanged("${profile}" "${isolated_bytes}" replay)
run_game(isolated_recording
    "Recorded [0-9]+ ticks.*checkpoint 0, sparks 0/2"
    --save "${profile}" --level "${first}" --record "${ROOT}/isolated.mjr" --frames 2)
require_unchanged("${profile}" "${isolated_bytes}" recording)
run_game(isolated_capture
    "Capture state: tick 1,.*checkpoint 0, sparks 0/2"
    --save "${profile}" --level "${first}" --capture-tick 1
    --capture-out "${ROOT}/isolated.png")
require_unchanged("${profile}" "${isolated_bytes}" capture)
run_game(isolated_script
    "Traveled through 'Back across the crossing' to progress-a"
    --save "${profile}" --level "${first}" --travel-test)
require_unchanged("${profile}" "${isolated_bytes}" scripted_travel)
run_game(saving_disabled "Rendered frames: 2"
    --save "${profile}" --no-save --level "${first}" --frames 2)
require_unchanged("${profile}" "${isolated_bytes}" no_save)

# The shared new-expedition handler must clear progress in every visited place.
run_game(reset "Expedition end: level progress-a, checkpoint 0, sparks 0/2"
    --save "${profile}" --level "${first}" --progress-test reset)
run_game(reset_first "Expedition start: level progress-a, checkpoint 0, sparks 0/2"
    --save "${profile}" --level "${first}" --progress-test inspect)
run_game(reset_second "Expedition start: level progress-b, checkpoint 0, sparks 0/2"
    --save "${profile}" --level "${second}" --progress-test inspect)

# Bad data is recoverable in play, but must survive untouched for recovery.
set(broken_bytes "{\"version\":1,\"entries\":[truncated expedition\n")
file(WRITE "${corrupt_profile}" "${broken_bytes}")
run_game(corrupt "Expedition start: level progress-a, checkpoint 0, sparks 0/2"
    --save "${corrupt_profile}" --level "${first}" --progress-test inspect)
if(NOT last_output MATCHES "Progress could not be loaded")
    message(FATAL_ERROR "The corrupt profile was not explained to the player")
endif()
require_unchanged("${corrupt_profile}" "${broken_bytes}" corrupt_profile)
run_game(recover_corrupt "Expedition end: level progress-a, checkpoint 0, sparks 0/2"
    --save "${corrupt_profile}" --level "${first}" --progress-test reset)
if(NOT last_output MATCHES "Progress saved")
    message(FATAL_ERROR "Starting a new expedition did not recover the corrupt profile")
endif()
file(READ "${corrupt_profile}" recovered)
string(JSON recovered_version GET "${recovered}" version)
if(NOT recovered_version EQUAL 1)
    message(FATAL_ERROR "Recovered expedition was not written in the supported format")
endif()
run_game(recovered_restart "Expedition start: level progress-a, checkpoint 0, sparks 0/2"
    --save "${corrupt_profile}" --level "${first}" --progress-test inspect)
if(last_output MATCHES "Progress could not be loaded")
    message(FATAL_ERROR "Recovered expedition could not be loaded by a subsequent process")
endif()

# Replacing a non-empty directory must fail on Linux and Windows. Reset is an
# explicit request to retry writing after the expected initial read error.
set(blocked_profile "${ROOT}/blocked expedition")
file(MAKE_DIRECTORY "${blocked_profile}")
set(marker "The existing directory and its contents must survive a failed save.\n")
file(WRITE "${blocked_profile}/keep.txt" "${marker}")
run_game(failed_save "Expedition end: level progress-a, checkpoint 0, sparks 0/2"
    --save "${blocked_profile}" --level "${first}" --progress-test reset)
if(NOT last_output MATCHES "Progress could not be saved" OR
   last_output MATCHES "Progress saved")
    message(FATAL_ERROR "A failed save was not accurately reported")
endif()
if(NOT IS_DIRECTORY "${blocked_profile}")
    message(FATAL_ERROR "A failed save replaced the existing directory")
endif()
require_unchanged("${blocked_profile}/keep.txt" "${marker}" failed_save)
