/*
 * Copyright (C) 2022 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "framework/ocl/opencl.h"
#include "framework/ocl/utility/profiling_helper.h"
#include "framework/test_case/register_test_case.h"
#include "framework/utility/timer.h"

#include "definitions/reduction.h"

static TestResult run(const ReductionArguments &arguments, Statistics &statistics) {
    QueueProperties queueProperties = QueueProperties::create().setProfiling(true);
    Opencl opencl(queueProperties);
    cl_int retVal;

    // Create kernel
    std::string kernelSource = R"(
       __kernel void reduction(__global uint *results) {
            if(get_global_id(0) != 0 )
            atomic_add(&results[0], results[get_global_id(0)]);
       }
    )";
    const char *pKernelSource = kernelSource.c_str();
    const size_t kernelSizes = kernelSource.size();
    cl_program program = clCreateProgramWithSource(opencl.context, 1, &pKernelSource, &kernelSizes, &retVal);
    ASSERT_CL_SUCCESS(retVal);
    ASSERT_CL_SUCCESS(clBuildProgram(program, 1, &opencl.device, nullptr, nullptr, nullptr));
    cl_kernel kernel = clCreateKernel(program, "reduction", &retVal);
    ASSERT_CL_SUCCESS(retVal);

    // Prepare data
    const size_t sizeInBytes = arguments.numberOfElements * sizeof(int);
    auto data = std::make_unique<int[]>(arguments.numberOfElements);
    size_t expectedSum = 0u;
    size_t value = 0u;
    for (auto i = 0u; i < arguments.numberOfElements; i++) {
        value++;
        if (value > 4)
            value = 0;
        data[i] = static_cast<int>(value);
        expectedSum += value;
    }

    // Create buffer
    cl_mem buffer = clCreateBuffer(opencl.context, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, sizeInBytes, data.get(), &retVal);
    ASSERT_CL_SUCCESS(retVal);

    // Validate results
    size_t actualSum;
    cl_event profilingEvent{};
    cl_ulong timeNs{};

    // Warmup kernel
    const size_t gws = arguments.numberOfElements;
    ASSERT_CL_SUCCESS(clSetKernelArg(kernel, 0, sizeof(buffer), &buffer));
    ASSERT_CL_SUCCESS(clEnqueueNDRangeKernel(opencl.commandQueue, kernel, 1, nullptr, &gws, nullptr, 0, nullptr, &profilingEvent));
    ASSERT_CL_SUCCESS(clFinish(opencl.commandQueue));

    ASSERT_CL_SUCCESS(clEnqueueReadBuffer(opencl.commandQueue, buffer, true, 0u, 4u, &actualSum, 0u, nullptr, nullptr));
    if (actualSum != expectedSum) {
        ASSERT_CL_SUCCESS(clReleaseMemObject(buffer));
        ASSERT_CL_SUCCESS(clReleaseKernel(kernel));
        ASSERT_CL_SUCCESS(clReleaseProgram(program));
        return TestResult::VerificationFail;
    }

    ASSERT_CL_SUCCESS(ProfilingHelper::getEventDurationInNanoseconds(profilingEvent, timeNs));
    ASSERT_CL_SUCCESS(clReleaseEvent(profilingEvent));
    statistics.pushValue(std::chrono::nanoseconds{timeNs}, MeasurementUnit::Microseconds, MeasurementType::Gpu, "time");
    statistics.pushValue(std::chrono::nanoseconds{timeNs}, sizeInBytes, MeasurementUnit::GigabytesPerSecond, MeasurementType::Gpu, "bw");

    // Benchmark
    for (auto i = 0u; i < arguments.iterations; i++) {
        int zero = 0u;
        ASSERT_CL_SUCCESS(clEnqueueWriteBuffer(opencl.commandQueue, buffer, true, 0u, 4u, &zero, 0u, nullptr, nullptr));
        ASSERT_CL_SUCCESS(clEnqueueNDRangeKernel(opencl.commandQueue, kernel, 1, nullptr, &gws, nullptr, 0, nullptr, &profilingEvent));
        ASSERT_CL_SUCCESS(clWaitForEvents(1, &profilingEvent));
        ASSERT_CL_SUCCESS(ProfilingHelper::getEventDurationInNanoseconds(profilingEvent, timeNs));
        ASSERT_CL_SUCCESS(clReleaseEvent(profilingEvent));
        if (i + 1 < arguments.iterations) {
            statistics.pushValue(std::chrono::nanoseconds{timeNs}, MeasurementUnit::Microseconds, MeasurementType::Gpu, "time");
            statistics.pushValue(std::chrono::nanoseconds{timeNs}, sizeInBytes, MeasurementUnit::GigabytesPerSecond, MeasurementType::Gpu, "bw");
        }
    }

    ASSERT_CL_SUCCESS(clReleaseMemObject(buffer));
    ASSERT_CL_SUCCESS(clReleaseKernel(kernel));
    ASSERT_CL_SUCCESS(clReleaseProgram(program));

    return TestResult::Success;
}

static RegisterTestCaseImplementation<Reduction> registerTestCase(run, Api::OpenCL);
