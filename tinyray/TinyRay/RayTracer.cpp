/*---------------------------------------------------------------------
*
* Copyright © 2015  Minsi Chen
* E-mail: m.chen@derby.ac.uk
*
* The source is written for the Graphics I and II modules. You are free
* to use and extend the functionality. The code provided here is functional
* however the author does not guarantee its performance.
---------------------------------------------------------------------*/
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>


#if defined(WIN32) || defined(_WINDOWS)
#include <Windows.h>
#include <gl/GL.h>
#endif

#ifdef __APPLE__
#include <OpenGL/gl.h>
#endif

#include "RayTracer.h"
#include "Ray.h"
#include "Scene.h"
#include "Camera.h"
#include "perlin.h"

RayTracer::RayTracer()
{
	m_buffHeight = m_buffWidth = 0.0;
	m_renderCount = 0;
	SetTraceLevel(5);
	m_traceflag = (TraceFlags)(TRACE_AMBIENT | TRACE_DIFFUSE_AND_SPEC |
		TRACE_SHADOW | TRACE_REFLECTION | TRACE_REFRACTION);

}

RayTracer::RayTracer(int Width, int Height)
{
	m_buffWidth = Width;
	m_buffHeight = Height;
	m_renderCount = 0;
	SetTraceLevel(5);

	m_framebuffer = new Framebuffer(Width, Height);

	//default set default trace flag, i.e. no lighting, non-recursive
	m_traceflag = (TraceFlags)(TRACE_AMBIENT);
}

RayTracer::~RayTracer()
{
	delete m_framebuffer;
}

void RayTracer::DoRayTrace( Scene* pScene )
{
	Camera* cam = pScene->GetSceneCamera();
	
	Vector3 camRightVector = cam->GetRightVector();
	Vector3 camUpVector = cam->GetUpVector();
	Vector3 camViewVector = cam->GetViewVector();
	Vector3 centre = cam->GetViewCentre();
	Vector3 camPosition = cam->GetPosition();

	double sceneWidth = pScene->GetSceneWidth();
	double sceneHeight = pScene->GetSceneHeight();

	double pixelDX = sceneWidth / m_buffWidth;
	double pixelDY = sceneHeight / m_buffHeight;
	
	int total = m_buffHeight*m_buffWidth;
	int done_count = 0;
	
	Vector3 start;

	start[0] = centre[0] - ((sceneWidth * camRightVector[0])
		+ (sceneHeight * camUpVector[0])) / 2.0;
	start[1] = centre[1] - ((sceneWidth * camRightVector[1])
		+ (sceneHeight * camUpVector[1])) / 2.0;
	start[2] = centre[2] - ((sceneWidth * camRightVector[2])
		+ (sceneHeight * camUpVector[2])) / 2.0;
	
	Colour scenebg = pScene->GetBackgroundColour();

	if (m_renderCount == 0)
	{
		fprintf(stdout, "Trace start.\n");

		Colour colour;
//TinyRay on multiprocessors using OpenMP!!!
#pragma omp parallel for schedule (dynamic, 1) private(colour)
		for (int i = 0; i < m_buffHeight; i+=1) {
			for (int j = 0; j < m_buffWidth; j+=1) {

				//calculate the metric size of a pixel in the view plane (e.g. framebuffer)
				Vector3 pixel;

				pixel[0] = start[0] + (i + 0.5) * camUpVector[0] * pixelDY
					+ (j + 0.5) * camRightVector[0] * pixelDX;
				pixel[1] = start[1] + (i + 0.5) * camUpVector[1] * pixelDY
					+ (j + 0.5) * camRightVector[1] * pixelDX;
				pixel[2] = start[2] + (i + 0.5) * camUpVector[2] * pixelDY
					+ (j + 0.5) * camRightVector[2] * pixelDX;

				/*
				* setup first generation view ray
				* In perspective projection, each view ray originates from the eye (camera) position 
				* and pierces through a pixel in the view plane
				*/
				Ray viewray;
				viewray.SetRay(camPosition,	(pixel - camPosition).Normalise());
				
				double u = (double)j / (double)m_buffWidth;
				double v = (double)i / (double)m_buffHeight;

				scenebg = pScene->GetBackgroundColour();

				//trace the scene using the view ray
				//default colour is the background colour, unless something is hit along the way
				colour = this->TraceScene(pScene, viewray, scenebg, m_traceLevel);

				/*
				* Draw the pixel as a coloured rectangle
				*/
				m_framebuffer->WriteRGBToFramebuffer(colour, j, i);
			}
		}

		fprintf(stdout, "Done!!!\n");
		m_renderCount++;
	}
}

Colour RayTracer::TraceScene(Scene* pScene, Ray& ray, Colour incolour, int tracelevel, bool shadowray)
{
	RayHitResult result;
	RayHitResult reflect_result;

	Ray surface_ray;
	Ray reflect_ray;
	Ray refract_ray;

	Colour shadow_col;
	Colour reflection_col;
	Colour refraction_col;

	Primitive *prim;
	Colour outcolour = incolour; //the output colour based on the ray-primitive intersection	

	std::vector<Light*> *light_list = pScene->GetLightList();
	Vector3 cameraPosition = pScene->GetSceneCamera()->GetPosition();

	//Intersect the ray with the scene
	//TODO: Scene::IntersectByRay needs to be implemented first
	result = pScene->IntersectByRay(ray);

	if (result.data && pScene ) //the ray has hit something
	{
		//TODO:
		//1. Non-recursive ray tracing:
		//	 When a ray intersect with an objects, determine the colour at the intersection point
		//	 using CalculateLighting
		outcolour = CalculateLighting(light_list, &cameraPosition, &result);
		
		//TODO: 2. The following conditions are for implementing recursive ray tracing
		if (m_traceflag & TRACE_REFLECTION)
		{
			//TODO: trace the reflection ray from the intersection point			
			reflect_ray.SetRay(reflect_ray.GetRayStart(), reflect_ray.GetRay()); // set ray
			reflection_col = TraceScene(pScene, reflect_ray, incolour, tracelevel, false); // set colour
			outcolour = outcolour + reflection_col; // change outcolours
		
		}

		if (m_traceflag & TRACE_REFRACTION)
		{
			//TODO: trace the refraction ray from the intersection point
		}

		if (m_traceflag & TRACE_SHADOW)
		{
			//TODO: trace the shadow ray from the intersection point
		}
	}
		
	return outcolour;
}

Colour RayTracer::CalculateLighting(std::vector<Light*>* lights, Vector3* campos, RayHitResult* hitresult)
{
	Colour outcolour;
	std::vector<Light*>::iterator lit_iter = lights->begin();

	// create colours
	Colour diffuze; 
	Colour specular;
	Colour lightC;

	Primitive* prim = (Primitive*)hitresult->data;
	Material* mat = prim->GetMaterial();

	// get value for diffuse and specular
	diffuze = mat->GetDiffuseColour();
	specular = mat->GetSpecularColour();

	Vector3 hitPos = hitresult->point; 
	Vector3 lightPosition; // light position
	Vector3 l; // light vector

	for (; lit_iter != lights->end(); ++lit_iter) // for loop for all lights
	{
		lightC = (*lit_iter)->GetLightColour(); // get light colour
		lightPosition = (*lit_iter)->GetLightPosition(); // get light position
		l = lightPosition - hitPos; // value for light vector
		l.Normalise();
	}

	Vector3 e = *campos - hitPos; // view vector
	Vector3 h = (l + e).Normalise(); // half vector


	outcolour = mat->GetAmbientColour();

	//Generate the grid pattern on the plane
	if (((Primitive*)hitresult->data)->m_primtype == Primitive::PRIMTYPE_Plane)
	{
		int dx = hitresult->point[0] / 2.0;
		int dy = hitresult->point[1] / 2.0;
		int dz = hitresult->point[2] / 2.0;

		if (dx % 2 || dy % 2 || dz % 2)
		{
			outcolour = Vector3(0.1, 0.1, 0.1);
		}
		else
		{
			outcolour = mat->GetDiffuseColour();
		}
		return outcolour;
	}

	////Go through all lights in the scene
	////Note the default scene only has one light source
	if (m_traceflag & TRACE_DIFFUSE_AND_SPEC)
	{
		//TODO: Calculate and apply the lighting of the intersected object using the illumination model covered in the lecture
		//i.e. diffuse using Lambertian model, for specular, you can use either Phong or Blinn-Phong model

		Colour DiffuseC; // diffuse colour
		Colour SpecularC; // specular colour
		Vector3 n = hitresult->normal; //get the normal

		float cos; // cos for diffuse
		double cos2; // cos for specular

		cos = n.DotProduct(l); //find the dot poruct of normal and light vector
		cos2 = n.DotProduct(h); // find the dot product of normal and half vector
		cos2 = pow(cos2, (mat->GetSpecPower() * 10));// cos for specular colour should be at the power of n,
													// n should be times 10 to make the light less intense.
		
		if (cos >= 0.0f && cos <= 1.0f)  // value of cos is between 0 and 1 
		{
			DiffuseC = diffuze*lightC*cos; // Calculate Diffuse colour
		}
		if(cos2 >= 0.0f && cos2 <= 1.0f) // value of cos is between 0 and 1
		{
			SpecularC=specular*lightC*cos2; //Calculate Specular colour
		}
		outcolour = DiffuseC + SpecularC; // final colour is the sum between diffuse and specular
	}

	return outcolour;
}

