package org.raytrace;

import java.awt.image.BufferedImage;
import java.awt.image.DataBufferInt;
import java.awt.image.RenderedImage;
import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStreamReader;
import java.util.ArrayList;
import java.util.List;

import javax.imageio.ImageIO;

import org.jocl.CL;
import org.jocl.Pointer;
import org.jocl.Sizeof;
import org.jocl.cl_command_queue;
import org.jocl.cl_context;
import org.jocl.cl_context_properties;
import org.jocl.cl_device_id;
import org.jocl.cl_image_format;
import org.jocl.cl_kernel;
import org.jocl.cl_mem;
import org.jocl.cl_platform_id;
import org.jocl.cl_program;

public class RaytraceOpenclDevice {
	private int numDevices = 0;
	private int deviceIndex = 0;
	private int animation = 0;
	private cl_context context;
	private cl_kernel kernel;
	private cl_platform_id platform;
	private cl_device_id device;
	private cl_command_queue commandQueue;
	private List<cl_mem> mem = new ArrayList<cl_mem>();
	private cl_mem outputImageMem;

	public RaytraceOpenclDevice(cl_platform_id platform, cl_device_id device, int numDevices, int deviceIndex, String kernelString, int animation) {
		this.platform = platform;
		this.device = device;
		this.numDevices = numDevices;
		this.deviceIndex = deviceIndex;
		this.animation = animation;
		setupKernel(kernelString);
		setupMemory();
		start();
	}

	private void setupKernel(String kernelString) {
		String deviceName = getString(device, CL.CL_DEVICE_NAME);

		cl_context_properties contextProperties = new cl_context_properties();
		contextProperties.addProperty(CL.CL_CONTEXT_PLATFORM, platform);

		context = CL.clCreateContext(contextProperties, 1, new cl_device_id[] { device }, null, null, null);

		cl_program program = CL.clCreateProgramWithSource(context, 1, new String[] { kernelString }, null, null);
		CL.clBuildProgram(program, 0, null, null, null, null);

		kernel = CL.clCreateKernel(program, "render", null);
	}

	private void setupMemory() {
		int i=0;
		makeWritableImage();
		for(cl_mem obj : mem) {
			CL.clSetKernelArg(kernel, i++, Sizeof.cl_mem, Pointer.to(obj));
		}
		CL.clSetKernelArg(kernel, i++, Sizeof.cl_int, Pointer.to(new int[]{getWidth()}));
		CL.clSetKernelArg(kernel, i++, Sizeof.cl_int, Pointer.to(new int[]{getHeight()}));
		CL.clSetKernelArg(kernel, i++, Sizeof.cl_int, Pointer.to(new int[]{getHeightOffset()}));
		CL.clSetKernelArg(kernel, i++, Sizeof.cl_int, Pointer.to(new int[]{animation}));
	}

	private void makeWritableImage() {
		cl_image_format imageFormat = new cl_image_format();
		imageFormat.image_channel_order = CL.CL_RGBA;
		imageFormat.image_channel_data_type = CL.CL_UNSIGNED_INT8;

		outputImageMem = CL.clCreateImage2D(context, CL.CL_MEM_WRITE_ONLY, new cl_image_format[]{imageFormat}, getWidth(), getHeightSlice(), 0, null, null);
		mem.add(outputImageMem);
	}

	private void start() {
		long globalWorkSize[] = new long[] { getHeightSlice(), getWidth() };

		commandQueue = CL.clCreateCommandQueue(context, device, 0, null);

		CL.clEnqueueNDRangeKernel(commandQueue, kernel, 2, null, globalWorkSize, null, 0, null, null);
	}


	public void waitToFinish(int[] d) throws IOException {
		CL.clEnqueueReadImage(
				commandQueue, outputImageMem, true, new long[3],
				new long[]{getWidth(), getHeightSlice(), 1},
				getWidth() * Sizeof.cl_uint, 0,
				Pointer.to(d).withByteOffset(getWidth() * Sizeof.cl_uint * getHeightOffset()), 0, null, null);

		CL.clReleaseCommandQueue(commandQueue);
		cleanup();
	}

	private static String getString(cl_device_id device, int paramName) {
		long size[] = new long[1];
		CL.clGetDeviceInfo(device, paramName, 0, null, size);
		byte buffer[] = new byte[(int)size[0]];
		CL.clGetDeviceInfo(device, paramName, buffer.length, Pointer.to(buffer), null);
		return new String(buffer, 0, buffer.length-1);
	}

	private void cleanup() {
		for(cl_mem obj : mem) {
			CL.clReleaseMemObject(obj);
		}
		CL.clReleaseKernel(kernel);
		CL.clReleaseContext(context);
	}

	private int getWidth() {
		return RaytraceOpencl.WIDTH;
	}

	private int getHeight() {
		return RaytraceOpencl.HEIGHT;
	}

	private int getHeightSlice() {
		return getHeight()/numDevices;
	}

	private int getHeightOffset() {
		return getHeightSlice()*deviceIndex;
	}
}
