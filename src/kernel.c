#pragma OPENCL EXTENSION cl_khr_fp64 : enable

#define PI (3.141592)
#define NULL (0)

typedef float real;
typedef float3 Vector;

typedef union {
	real  vecAccess[3];
	Vector vec;
} VectorAccess;

#define SPHERE 0
#define LIGHT 1
#define CHECKER 2
typedef struct {
	Vector diffuse, specular;
	real reflection, refraction;
	int specularRoughness;
} Surface;
typedef struct {
	Vector from, ray;
} Ray;
typedef struct {
	int type;
	real radius;
	Vector center, light, normal;
	Surface surface;
} Item;
typedef Item __attribute__((address_space(2))) * ItemPtr;
//typedef Item * ItemPtr;
typedef struct {
	int depth;
	Ray ray;
	real magnitude, refractiveIndex;
} StackEntry;
typedef struct {
	StackEntry entries[100];
	int size;
} Stack;
typedef struct {
	Ray ray;
	Vector vVector, hVector;
} Viewport;
#define MAX_LIGHTS (8)
#define MAX_ITEMS (16)

// ************************* MOVE THESE TO THE JAVA SIDE *****************************
__constant real hFov = PI/2*(.9);
__constant Vector X = {1, 0, 0};
__constant Vector Y = {0, 1, 0};
__constant Vector Z = {0, 0, 1};
__constant Vector ZERO = {0, 0, 0};
#define ZERO_DEF {0, 0, 0}
__constant Vector ONE = {1, 1, 1};
#define ONE_DEF {1, 1, 1}
#define RED {1, 0, 0}
#define GREEN {0, 1, 0}
#define BLUE {0, 0, 1}
#define GRAY {.5, .5, .5}
#define YELLOW {1, 1, 0}

#define DIM (.5)
#define INTENSITY (.5)
__constant Vector camera = {0, 1.25, -2.5};
__constant Vector lookAt = {0, .5, .5};

#define SURFACE_LIGHT {ZERO_DEF, ZERO_DEF, 0, 0, 100}
//diffuse, specular:
#define SURFACE_SPHERE1 { \
	.02, .02, .02, \
	.15, .15, .15, \
	.05, 1.9, 100} //reflection, refraction, specularRoughness
#define SURFACE_SPHERE2 {.5, 0, 0, .5, 0, 0, .4, 0, 100}
#define SURFACE_SPHERE3 {0, 0, .9, .5, 0, 0, .4, 0, 100}
#define SURFACE_CHECKER {ONE_DEF, .5, .5, .5, .4, 0, 100}
#define lightNumber (4)
#define itemNumber (lightNumber+4)
__constant Item lights[lightNumber] = {
	{LIGHT, 1, 3, 3, -4, (DIM+.25)*INTENSITY, (DIM+.25)*INTENSITY, (DIM)*INTENSITY, ZERO_DEF, SURFACE_LIGHT},
	{LIGHT, 1, 0, 3, 0, (DIM)*INTENSITY, (DIM)*INTENSITY, (DIM+.1)*INTENSITY, ZERO_DEF, SURFACE_LIGHT},
	{LIGHT, 1, -3, 3, 0, (DIM)*INTENSITY, (DIM+.2)*INTENSITY, (DIM)*INTENSITY, ZERO_DEF, SURFACE_LIGHT},
	{LIGHT, 1, 3, 2.5, 3, (DIM*.5)*INTENSITY, (DIM*.5)*INTENSITY, (DIM*.5)*INTENSITY, ZERO_DEF, SURFACE_LIGHT}
};
__constant Item items[itemNumber] = {
	{SPHERE, .5, //radius
		1, 1, -.8, //center
		ZERO_DEF, ZERO_DEF, SURFACE_SPHERE1},
	{SPHERE, 1, //radius
		-1, 1, 3, //center
		ZERO_DEF, //light
		ZERO_DEF, //normal
		SURFACE_SPHERE2
	},
	{SPHERE, 1, //radius
		-9, 1, 5, //center
		ZERO_DEF, //light
		ZERO_DEF, //normal
		SURFACE_SPHERE3
	},
	{CHECKER, 0, 0, 0, 0, ZERO_DEF, 0, 1, 0, SURFACE_CHECKER},
	{LIGHT, 1, 3, 3, -4, (DIM+.25)*INTENSITY, (DIM+.25)*INTENSITY, (DIM)*INTENSITY, ZERO_DEF, SURFACE_LIGHT},
	{LIGHT, 1, 0, 3, 0, (DIM)*INTENSITY, (DIM)*INTENSITY, (DIM+.1)*INTENSITY, ZERO_DEF, SURFACE_LIGHT},
	{LIGHT, 1, -3, 3, 0, (DIM)*INTENSITY, (DIM+.2)*INTENSITY, (DIM)*INTENSITY, ZERO_DEF, SURFACE_LIGHT},
	{LIGHT, 1, 3, 2.5, 3, (DIM*.5)*INTENSITY, (DIM*.5)*INTENSITY, (DIM*.5)*INTENSITY, ZERO_DEF, SURFACE_LIGHT}
};

#define SHADOW_RUNS 6
#define PIXEL_RUNS 15
#define MAX_DEPTH 6
// ************************* MOVE THESE TO THE JAVA SIDE *****************************
void push(Stack * stack, int depth, Ray ray, real magnitude, real refractiveIndex) {
	StackEntry * entry = &stack->entries[stack->size++];
	entry->depth=depth;
	entry->ray=ray;
	entry->magnitude = magnitude;
	entry->refractiveIndex = refractiveIndex;
}
StackEntry * pop(Stack *stack) {
	return &stack->entries[--stack->size];
}
int getOn(VectorAccess position) {
	int x = (int)position.vecAccess[0] + (position.vecAccess[0]<0 ? 1 : 0);
	int z = (int)position.vecAccess[2] + (position.vecAccess[2]<0 ? 1 : 0);
	return (x + z)%2!=0;
}
int getSpecularRoughness(ItemPtr item, Vector point) {
	return item->surface.specularRoughness;
}
Vector getDiffuse(ItemPtr item, Vector point) {
	if(item->type==CHECKER && !getOn((VectorAccess)point)) {
		return ZERO;
	}
	return item->surface.diffuse;
}
Vector getSpecular(ItemPtr item, Vector point) {
	return item->surface.specular;
}
real getReflection(ItemPtr item, Vector point) {
	return item->surface.reflection;
}
real getRefraction(ItemPtr item, Vector point) {
	return item->surface.refraction;
}


//VECTOR MATH
real length2(Vector v) {
	return dot(v, v);
}
Vector reflect(Vector vec, Vector normal) {
	return vec + normal*(-2*dot(vec, normal));
}
Vector refract(Vector vec, Vector normal, ItemPtr item, real refractiveIndexA) {
	real refractiveIndexB = getRefraction(item, ZERO);
	//http://en.wikipedia.org/wiki/Refractive_index
	//http://en.wikipedia.org/wiki/Refraction
	real refractiveRatio = refractiveIndexA / refractiveIndexB;
	real cosThetaA = -dot(normal, vec);
	real sinThetaA2 = 1 - cosThetaA * cosThetaA;
	real cosThetaB2 = 1 - refractiveRatio * refractiveRatio * sinThetaA2;

	if(cosThetaB2 < 0) {
		//no refraction
		return ZERO;
	}
	real cosThetaB = sqrt(cosThetaB2);
	return vec * refractiveRatio + normal * (refractiveRatio * cosThetaA - cosThetaB);
}
/*
ulong next(ulong * seed, int bits) {
	*seed=(*seed * 0x5DEECE66DUL + 0xBUL) & ((1UL << 48) - 1);
	return (ulong)(*seed >> (48 - bits));
}
real random(ulong *seed) {
	//from java.util.Random.nextDouble
	//return ((next(seed, 26) << 27) + next(seed, 27)) / (real)(1UL << 53);
	//from java.util.Random.nextFloat
	return next(seed, 24) / ((real)(1UL << 24));
}
*/
//push(@_, rand()-.5) foreach (0..255); printf "%.8f, ", $_ foreach(@_); print; printf "%.8f, ", (2*rand()-1)*sqrt(.5**2-$_**2) foreach(@_); print
__constant real randomXValues[256] = {
	0.04774635, 0.23499631, 0.40105514, -0.25842175, 0.38398280, -0.25171711, -0.36197213, -0.25201553, 0.29906780, -0.30148849, -0.26898939, 0.08444336, 0.14489726, 0.09365446, -0.06756723, -0.48409336, 0.42004063, 0.18993764, 0.23507176, 0.44849234, 0.14940339, -0.35548170, -0.11069749, -0.05346393, -0.38208582, -0.30259826, 0.36498431, 0.26520522, -0.21875639, -0.32774518, 0.07128899, 0.33679617, -0.19079003, -0.11374607, -0.34405791, -0.07226130, 0.48329329, 0.33816176, -0.02787849, -0.13096525, 0.12032102, -0.05693813, 0.26120797, -0.27647419, 0.45978822, 0.25881970, -0.10959761, -0.00219811, 0.49044136, -0.47376552, 0.19673857, -0.06584916, -0.46118003, -0.13952463, -0.08283068, -0.43126208, 0.46267165, -0.21014158, 0.07526240, 0.04686627, 0.42791767, 0.25904534, 0.49880270, -0.28401144, 0.26965478, 0.18268184, -0.23741900, -0.26596796, -0.30583241, -0.34668046, -0.10669823, 0.43710371, -0.25314707, -0.25921789, -0.30156671, 0.49992181, -0.19479515, 0.25117054, -0.14935162, -0.37183491, 0.43958548, -0.10969479, -0.40991651, -0.33124512, 0.47854423, -0.43794905, 0.01692282, 0.27692727, -0.49079496, 0.35576639, -0.19113662, -0.30652359, 0.40409997, 0.48378804, -0.11296870, 0.45804552, -0.31070348, -0.37024686, 0.09821978, -0.39827731, 0.12837718, -0.01233875, -0.38765001, -0.24799699, -0.40545792, -0.39852681, 0.13638881, -0.23642753, -0.48129964, 0.36297460, -0.39271413, -0.28186096, 0.14290271, 0.36444151, 0.08763834, 0.06509897, -0.23802259, -0.07918720, -0.30730006, 0.09221891, -0.28397534, -0.07229767, -0.47690921, 0.00240172, 0.29488001, -0.22360364, 0.46433372, 0.45027519, -0.04671276, -0.45966672, -0.00454160, 0.17271122, 0.41095458, -0.16957248, -0.31917485, 0.18298922, -0.24833416, -0.39441937, 0.11159375, 0.13232342, -0.45493971, 0.35204538, -0.27140462, -0.24132936, 0.12659222, -0.29117393, 0.16257816, -0.38446474, 0.39666377, -0.37051472, -0.36016667, 0.41793188, 0.07825164, 0.11268701, 0.24314147, -0.33558360, 0.12090298, -0.00073614, -0.28499997, -0.36055631, -0.49673557, 0.18290395, 0.32052650, 0.30616643, -0.21003484, 0.19068801, -0.46727964, -0.05630335, 0.49345727, -0.39972997, -0.10251782, 0.03297095, 0.20720702, 0.32971223, 0.01762466, -0.13951722, 0.07143739, -0.11329040, -0.24511062, 0.48868378, -0.05742201, 0.13464579, 0.28064849, 0.17585053, -0.47305951, -0.27228620, -0.24340596, 0.44732694, 0.15028995, -0.19406273, 0.14649388, -0.10442476, 0.03843130, -0.19130681, -0.14765780, 0.38031368, 0.32831003, -0.13675891, -0.36059939, -0.28527353, 0.19119324, 0.39940875, 0.22428221, -0.34753561, -0.31586437, -0.28480338, 0.21107167, 0.16670256, 0.33615168, -0.28150591, -0.01168066, -0.33870113, 0.18244372, 0.06082761, 0.16354215, 0.04014173, 0.47293282, 0.18918349, -0.01632130, 0.03975269, -0.33241076, 0.31619587, -0.45502383, 0.09840840, 0.48616838, 0.05771181, -0.03764778, 0.37937946, -0.32243965, 0.13591972, -0.22217826, -0.38610790, 0.11581385, 0.48745383, -0.07532363, -0.20073520, -0.30411911, -0.24360547, 0.41644896, -0.26185434, 0.00025588, 0.16472230, 0.43810696, 0.08529189, -0.37591653, 0.32602893, -0.27465815, 0.26892833, 0.49524235, -0.34375459, -0.47667179, 0.05506261, 0.19975914, -0.39246294, 0.21910996, -0.05349133
};
__constant real randomYValues[256] = {
	-0.25664545, 0.21510319, 0.05682318, 0.22745379, -0.04238593, 0.37870903, -0.06068635, 0.01219593, -0.29164922, -0.21759172, 0.00441985, 0.17234802, -0.10000088, 0.11499332, -0.21052187, 0.10290839, -0.03987816, -0.24238440, 0.39671954, 0.17236432, 0.31950392, 0.31548830, -0.08099213, -0.36437994, -0.21029280, 0.34075613, -0.08542808, -0.25816401, -0.27049940, -0.36018056, -0.26511678, -0.00810261, -0.29564879, 0.01748522, -0.32659716, 0.13550188, 0.01938860, 0.31809367, 0.18765476, -0.28082914, 0.30207489, -0.42550300, -0.30383039, -0.37041866, 0.18395315, 0.02603271, -0.06042202, 0.12870670, 0.06669629, -0.14587362, 0.00645545, -0.37760982, -0.15152412, -0.43624871, 0.18959383, -0.02768216, 0.02568886, 0.08608959, -0.16179447, -0.42186128, 0.09502285, 0.25300950, -0.02612312, 0.31288975, -0.05811906, -0.08179342, 0.05497006, -0.28064030, 0.38282115, 0.02980118, -0.44801118, -0.03590712, 0.32615747, 0.23897470, -0.30963705, 0.00678539, 0.40260335, -0.41577231, -0.38762501, 0.16222219, 0.21134093, 0.43880545, -0.19777760, -0.37164441, -0.01142475, -0.23026168, 0.36013575, -0.26623026, -0.03137119, -0.03988092, -0.39135412, 0.16612960, 0.19653955, 0.12360618, -0.37858801, -0.05765586, -0.05366249, 0.17463819, -0.30548971, 0.20641065, 0.04236206, -0.07196221, 0.26501726, -0.27137430, 0.07810010, -0.22083314, -0.36139741, 0.23867911, 0.04792545, 0.12502873, -0.01061651, 0.24004119, -0.46861082, -0.07846537, -0.03311606, -0.22788089, -0.03033182, -0.04453557, -0.15698294, 0.19773489, -0.40812638, 0.11361675, -0.03445290, -0.34765236, -0.36638803, -0.21902004, -0.07355234, 0.18923009, -0.25611727, -0.00105810, 0.48684595, -0.04286538, 0.18738188, -0.09065969, 0.08891500, 0.01750576, -0.25246720, -0.20359160, 0.11066650, -0.08792126, -0.15214918, 0.03235956, 0.38970418, -0.22410819, 0.05364750, -0.18830997, 0.06460389, -0.01183504, 0.25097194, 0.10693737, -0.31669109, 0.04258908, -0.12133539, 0.01884779, 0.08595525, -0.14521704, -0.05212374, -0.09434168, 0.29693925, 0.23534061, 0.01571724, -0.14637501, -0.38010313, -0.06157797, 0.33318206, -0.19744685, 0.07685736, -0.49180369, -0.04130323, -0.06541834, -0.06914365, -0.10739460, -0.22699053, -0.01082076, -0.10600553, 0.22067248, 0.47056819, 0.05998143, -0.25211738, 0.08819798, 0.30029221, -0.03940292, -0.24136856, 0.00289658, 0.03001054, -0.41821525, 0.17264499, -0.05412246, -0.13882172, -0.17997488, 0.18910294, -0.35095857, 0.45755676, 0.32773566, -0.41809797, -0.09179868, 0.36853788, 0.19057004, -0.23349188, -0.25091236, 0.41413812, -0.29057512, 0.41632020, -0.03471441, -0.30552474, 0.17305959, 0.21034566, -0.26076898, -0.19445211, 0.37886246, 0.39129660, 0.35619759, 0.16348376, -0.35831016, -0.17641432, -0.18867622, 0.10461785, 0.24153519, -0.41174253, -0.47193952, 0.15894429, -0.33034988, 0.04940866, 0.12570673, -0.05962030, 0.36718199, 0.33847549, -0.03598125, -0.18638146, -0.18992152, -0.06484180, -0.09517560, -0.42368234, 0.04546939, -0.28397792, 0.08707554, -0.19840740, 0.27476154, -0.06418433, 0.18421669, -0.42560066, -0.28616287, 0.01423122, 0.16566546, 0.17742946, -0.30947008, 0.36874115, 0.07216945, -0.05282819, 0.02049993, 0.01499959, 0.11834028, 0.42777352, -0.23939075, -0.01343282, -0.10247859
};
real randomx(int i) {
	return randomXValues[i&0xff];
}
real randomy(int i) {
	return randomYValues[i&0xff];
}

Vector getIntersectPointSphere(ItemPtr item, Ray ray) {
	Vector centerRay = item->center - ray.from;
	real dotValue = dot(ray.ray, centerRay);
	if(dotValue<0) {
		//behind
		return ZERO;
	}
	//http://en.wikipedia.org/wiki/Line%E2%80%93sphere_intersection
	real discriminant = dotValue*dotValue - length2(centerRay) + item->radius*item->radius;
	if(discriminant<0) {
		//does not intersect
		return ZERO;
	}
	discriminant=sqrt(discriminant);

	real close = dotValue - discriminant;
	real further = dotValue + discriminant;

	return ray.ray * (close > 0 ? close : further);
}


Vector getIntersectPointPlane(ItemPtr item, Ray ray) {
	real u = dot(item->normal, item->center - ray.from) / dot(item->normal, ray.ray);
	return u<0 ? ZERO : ray.ray*u;
}
	

Vector getIntersectPoint(ItemPtr item, Ray ray) {
	switch(item->type) {
	case SPHERE:
	case LIGHT:
		return getIntersectPointSphere(item, ray);
	case CHECKER:
		return getIntersectPointPlane(item, ray);
	default:
		return ZERO;
	}
}

Vector getNormal(ItemPtr item, Ray ray, Vector point) {
	switch(item->type) {
	case SPHERE:
	case LIGHT:
		if(length2(ray.from-item->center)<item->radius * item->radius) {
			//inside!
			return fast_normalize(item->center-point);
		}
		return fast_normalize(point-item->center);
	case CHECKER:
		return item->normal;
	default:
		return ZERO;
	}
}

ItemPtr getClosestItem(Ray ray) {
	ItemPtr closestItem = NULL;
	real distance2 = 1e300;
	for(int i=0;i<itemNumber;i++) {
		Vector intersection = getIntersectPoint(&items[i], ray);
		real cur2 = length2(intersection);
		if(cur2 != 0 && cur2 < distance2) {
			distance2 = cur2;
			closestItem = &items[i];
		}
	}
	return closestItem;
}

real cot(real theta) {return tan(PI/2 - theta);}

Vector getUp(Vector forward, Vector right) {
	return cross(forward, right);
}

Vector getRight(Vector forward) {
	Vector right = cross(Y, forward);
	return length2(right)==0 ? X : fast_normalize(right);
}

Vector getPixel(Ray ray, Stack * stack, real magnitude, real refractiveIndex, int depth, ulong *seed) {
	ItemPtr item = getClosestItem(ray);

	if(item==NULL) {
		return ZERO;
	}

	Vector point = getIntersectPoint(item, ray) + ray.from;
	int specularRoughness = getSpecularRoughness(item, point);
	Vector normal = getNormal(item, ray, point);

	Vector diffuseColor = ZERO;
	Vector specularColor = ZERO;

	Ray reflection;
	reflection.ray = reflect(ray.ray, normal);
	reflection.from = point+(reflection.ray*.001f);

	if(length2(getDiffuse(item, point))>0 || length2(getSpecular(item, point))>0) {
		for(int i=0;i<lightNumber;i++) {
			ItemPtr light = &lights[i];
			Vector lightVector = light->center-point;
			Vector lightPixel = fast_normalize(lightVector);

			real diffuseFactor = dot(normal, lightPixel);
			real specularFactor = dot(reflection.ray, lightPixel);

			if(diffuseFactor>0 || specularFactor>0) {
				int hitLight=0;
				
				Ray movedLightRay;
				movedLightRay.from=reflection.from;
				
				Vector right = getRight(lightPixel);
				Vector up = getUp(lightPixel, right);
				for(int i=0;i<SHADOW_RUNS;i++) {
					//TODO:  this can be much faster methinks
					//real r = random(seed)*2-1;
					//real u = random(seed)*sqrt(1-r*r);
					real r = randomx(i)*2;
					real u = randomy(i)*2;
					movedLightRay.ray = fast_normalize(lightVector+right*r*light->radius+up*u*light->radius);

					ItemPtr closestItem = getClosestItem(movedLightRay);
					if(closestItem!=NULL && closestItem->type==LIGHT) {
						hitLight++;
					}
				}
				if(hitLight > 0) {
					real lightValue = ((real)hitLight) / SHADOW_RUNS;
					if(diffuseFactor>0) {
						diffuseFactor *= lightValue;
						diffuseColor = diffuseColor+light->light*diffuseFactor;
					}
					if(specularFactor>0) {
						specularFactor = lightValue * pow(specularFactor, specularRoughness);
						specularColor = specularColor+light->light*specularFactor;
					}
				}
			}
		}
	}

	Vector color = (diffuseColor*getDiffuse(item, point)+specularColor*getSpecular(item, point)) * magnitude;

	if(depth > 0) {
		Ray refraction;
		if(getRefraction(item, point)) {
			refraction.ray = refract(ray.ray, normal, item, refractiveIndex);
			refraction.from = point+(refraction.ray*.001f);
			if(length2(refraction.ray)>0) {
				push(stack, depth-1, refraction, magnitude, getRefraction(item, point));
			}
		}

		if(getReflection(item, point) > 0) {
			push(stack, depth-1, reflection, magnitude * getReflection(item, point), refractiveIndex);
		}
	}

	return color;
}

Vector getPixelWithReflection(Ray ray, Stack * stack, ulong * seed) {
	push(stack, MAX_DEPTH, ray, 1, 1);
	Vector color = ZERO;

	while(stack->size>0) {
		StackEntry * entry = pop(stack);
		color += getPixel(entry->ray, stack, entry->magnitude, entry->refractiveIndex, entry->depth, seed);
	}

	return color;
}

Vector getPixelAntialiased(Viewport viewport, Stack * stack, ulong * seed) {
	Vector totalColor = ZERO;
	Ray ray = viewport.ray;
	for(int i=0;i<PIXEL_RUNS;i++) {
		Vector hError = viewport.hVector*randomx(i);
		Vector vError = viewport.vVector*randomy(i);
		ray.ray = fast_normalize(viewport.ray.ray+(i==0?0:hError+vError));
		Vector color = getPixelWithReflection(ray, stack, seed);
		totalColor += color;
	}
	totalColor = totalColor*(1.f/PIXEL_RUNS);
	return totalColor;
}

Viewport setupViewport(int x, int y, int w, int h, int animation) {
	Viewport viewport;
	Vector forward = fast_normalize(lookAt-(camera+X*animation/40.f));

	real distanceToViewport = cot(hFov/2)/2 * w;
	viewport.hVector = getRight(forward);
	viewport.vVector = getUp(forward, viewport.hVector)*-1;
	viewport.ray.from = camera+X*animation/40.f;
	viewport.ray.ray = forward*distanceToViewport+viewport.hVector*(-w/2+x)+viewport.vVector*(-h/2+y);
	return viewport;
}

__kernel void render(__write_only __global image2d_t targetImage, int w, int h, int heightOffset, int animation) {
	int y = get_global_id(0)+heightOffset;
	int x = get_global_id(1);
	Stack stack;
	stack.size=0;
	ulong seed = y*w+x;

	Viewport viewport = setupViewport(x, y, w, h, animation);
	Vector color = getPixelAntialiased(viewport, &stack, &seed);
	uint4 intColor = {color.z*255, color.y*255, color.x*255, 255};

	int2 posOut = {x, y-heightOffset};
	write_imageui(targetImage, posOut, intColor);
}

