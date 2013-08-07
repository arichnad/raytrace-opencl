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
					real r = random(seed)*2-1;
					real u = random(seed)*sqrt(1-r*r);
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
		Vector hError = viewport.hVector*(random(seed)-.5f);
		Vector vError = viewport.vVector*(random(seed)-.5f);
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

__kernel void render(__write_only __global image2d_t targetImage, int w, int h, int animation) {
	int y = get_global_id(0);
	int x = get_global_id(1);
	Stack stack;
	stack.size=0;
	ulong seed = y*w+x;

	Viewport viewport = setupViewport(x, y, w, h, animation);
	Vector color = getPixelAntialiased(viewport, &stack, &seed);
	uint4 intColor = {color.z*255, color.y*255, color.x*255, 255};

	int2 posOut = {x, y};
	write_imageui(targetImage, posOut, intColor);
}

