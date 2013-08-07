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

public class RaytraceOpencl {
	private static final int PLATFORM_INDEX = 0;
	private static final int DEVICE_INDEX = 0;
	private static final int WIDTH = 1800;
	private static final int HEIGHT = 1000;
	private cl_context context;
	private cl_kernel kernel;
	private cl_device_id device;
	private cl_mem outputImageMem;
	private List<cl_mem> mem = new ArrayList<cl_mem>();
	private int animation = 0;
	private String filename = "output.png";

	public static void main(String[] args) throws IOException {
		RaytraceOpencl render = new RaytraceOpencl(readFile("src/kernel.c"));
		if(args.length>=1) {
			render.filename=args[0];
		}
		if(args.length>=2) {
			render.animation=Integer.parseInt(args[1]);
		}
		render.setupAndRun();
		render.cleanup();
	}

	private RaytraceOpencl(String kernelString) {
		setupKernel(kernelString);
	}

	private void setupAndRun() throws IOException {
		setupMemory();
		RenderedImage outputImage = run();
		ImageIO.write(outputImage, "png", new File(filename));
	}

	private void setupKernel(String kernelString) {
		CL.setExceptionsEnabled(true);

		cl_platform_id platforms[] = new cl_platform_id[PLATFORM_INDEX+1];
		CL.clGetPlatformIDs(platforms.length, platforms, null);
		cl_platform_id platform = platforms[PLATFORM_INDEX];

		cl_device_id devices[] = new cl_device_id[DEVICE_INDEX+1];
		CL.clGetDeviceIDs(platform, CL.CL_DEVICE_TYPE_GPU, DEVICE_INDEX+1, devices, null);
		device = devices[DEVICE_INDEX];

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
		CL.clSetKernelArg(kernel, i++, Sizeof.cl_int, Pointer.to(new int[]{WIDTH}));
		CL.clSetKernelArg(kernel, i++, Sizeof.cl_int, Pointer.to(new int[]{HEIGHT}));
		CL.clSetKernelArg(kernel, i++, Sizeof.cl_int, Pointer.to(new int[]{animation}));
	}

	private void makeWritableImage() {
		cl_image_format imageFormat = new cl_image_format();
		imageFormat.image_channel_order = CL.CL_RGBA;
		imageFormat.image_channel_data_type = CL.CL_UNSIGNED_INT8;

		outputImageMem = CL.clCreateImage2D(context, CL.CL_MEM_WRITE_ONLY, new cl_image_format[]{imageFormat}, WIDTH, HEIGHT, 0, null, null);
		mem.add(outputImageMem);
	}

	private RenderedImage run() throws IOException {
		long globalWorkSize[] = new long[] { HEIGHT, WIDTH };
		//long globalWorkSize[] = new long[] { HEIGHT };

		cl_command_queue commandQueue = CL.clCreateCommandQueue(context, device, 0, null);

		System.out.println("ray tracing");
		CL.clEnqueueNDRangeKernel(commandQueue, kernel, 2, null, globalWorkSize, null, 0, null, null);

		BufferedImage outputImage = new BufferedImage(WIDTH, HEIGHT, BufferedImage.TYPE_INT_RGB);

		DataBufferInt dataBufferDst = (DataBufferInt)outputImage.getRaster().getDataBuffer();
		int[] d = dataBufferDst.getData();
		for(int i=0;i<d.length;i++) {
			d[i]=0;
		}

		CL.clEnqueueReadImage(
				commandQueue, outputImageMem, true, new long[3],
				new long[]{WIDTH, HEIGHT, 1},
				WIDTH * Sizeof.cl_uint, 0,
				Pointer.to(dataBufferDst.getData()), 0, null, null);

		CL.clReleaseCommandQueue(commandQueue);

		return outputImage;
	}

	private void cleanup() {
		for(cl_mem obj : mem) {
			CL.clReleaseMemObject(obj);
		}
		CL.clReleaseKernel(kernel);
		CL.clReleaseContext(context);
	}
	private static String readFile(String fileName) throws IOException {
		BufferedReader reader = new BufferedReader(new InputStreamReader(new FileInputStream(fileName)));
		StringBuffer buffer = new StringBuffer();
		String line;
		while((line = reader.readLine()) != null) {
			buffer.append(line).append("\n");
		}
		reader.close();
		return buffer.toString();
	}
}
