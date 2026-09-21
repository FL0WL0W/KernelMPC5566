namespace
{
	[[noreturn]] void NoExceptionFailure()
	{
		asm volatile("wrteei 0" ::: "memory");
		for (;;)
			asm volatile("" ::: "memory");
	}
}

// libstdc++ containers and std::function call these helpers for failures that
// would normally throw. Exceptions are disabled in the kernel, so provide the
// terminal behavior directly without pulling in libstdc++'s formatting and
// locale support through functexcept.o.
namespace std
{
	[[noreturn]] void __throw_bad_alloc()
	{
		NoExceptionFailure();
	}

	[[noreturn]] void __throw_length_error(const char*)
	{
		NoExceptionFailure();
	}

	[[noreturn]] void __throw_bad_function_call()
	{
		NoExceptionFailure();
	}
}
