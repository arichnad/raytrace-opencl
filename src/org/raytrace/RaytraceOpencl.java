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
	public static final int WIDTH = 1800;
	public static final int HEIGHT = 1000; //XXX:  assumes that HEIGHT is a multiple of the number of devices!
	
	private static final int PLATFORM_INDEX = 0;
	private int numDevices = 0;
	private RaytraceOpenclDevice[] raytraceDevices;
	private int animation = 0;
	private String filename = "output.png";

	public static void main(String[] args) throws IOException {
		RaytraceOpencl render = new RaytraceOpencl();
		if(args.length>=1) {
			render.filename=args[0];
		}
		if(args.length>=2) {
			render.animation=Integer.parseInt(args[1]);
		}
		render.setupAndRun();
	}

	private void setupAndRun() throws IOException {
		setupKernel(readFile("src/kernel.c"));
		RenderedImage outputImage = waitToFinish();
		ImageIO.write(outputImage, "png", new File(filename));
	}

	private void setupKernel(String kernelString) {
		CL.setExceptionsEnabled(true);

		cl_platform_id platforms[] = new cl_platform_id[PLATFORM_INDEX+1];
		CL.clGetPlatformIDs(platforms.length, platforms, null);
		cl_platform_id platform = platforms[PLATFORM_INDEX];

		int numDevicesArray[] = new int[1];
		CL.clGetDeviceIDs(platform, CL.CL_DEVICE_TYPE_GPU, 0, null, numDevicesArray);
		numDevices = numDevicesArray[0];

		cl_device_id[] devices = new cl_device_id[numDevices];
		CL.clGetDeviceIDs(platform, CL.CL_DEVICE_TYPE_GPU, numDevices, devices, null);
		
		raytraceDevices = new RaytraceOpenclDevice[numDevices];

		for (int i=0;i<numDevices;i++) {
			raytraceDevices[i] = new RaytraceOpenclDevice(platform, devices[i], numDevices, i, kernelString, animation);
		}
	}

	private RenderedImage waitToFinish() throws IOException {
		BufferedImage outputImage = new BufferedImage(WIDTH, HEIGHT, BufferedImage.TYPE_INT_RGB);

		DataBufferInt dataBufferDst = (DataBufferInt)outputImage.getRaster().getDataBuffer();
		int[] d = dataBufferDst.getData();
		for(int i=0;i<d.length;i++) {
			d[i]=0;
		}
		System.out.println("waiting to finish");
		for(int i=0;i<numDevices;i++) {
			raytraceDevices[i].waitToFinish(d);
		}
		System.out.println("finished");

		return outputImage;
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
