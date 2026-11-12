# One sparse source checkout supplies the host compiler/linker and the target
# C++ libraries/builtins. Git's partial clone avoids fetching unrelated blobs.
if(NOT EXISTS "${SOURCE_DIR}/.git")
    execute_process(
        COMMAND "${GIT}" clone --depth 1 --filter=blob:none --sparse --no-checkout
            --single-branch --branch main https://github.com/FL0WL0W/llvm-project.git "${SOURCE_DIR}"
        COMMAND_ERROR_IS_FATAL ANY
    )
endif()
# Refresh main on every configure, including already-populated checkouts.
execute_process(
    COMMAND "${GIT}" -C "${SOURCE_DIR}" fetch --depth 1 origin main
    COMMAND_ERROR_IS_FATAL ANY
)
execute_process(
    COMMAND "${GIT}" -C "${SOURCE_DIR}" checkout --detach FETCH_HEAD
    COMMAND_ERROR_IS_FATAL ANY
)
execute_process(
    COMMAND "${GIT}" -C "${SOURCE_DIR}" sparse-checkout set
        cmake llvm clang lld libc libunwind third-party runtimes libcxx libcxxabi compiler-rt
    COMMAND_ERROR_IS_FATAL ANY
)
