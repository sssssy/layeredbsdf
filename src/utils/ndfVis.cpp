/*
    This file is part of Mitsuba, a physically based rendering system.

    Copyright (c) 2007-2014 by Wenzel Jakob and others.

    Mitsuba is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License Version 3
    as published by the Free Software Foundation.

    Mitsuba is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include <mitsuba/render/util.h>
#include <mitsuba/core/timer.h>
#include <mitsuba/core/fresolver.h>
#include <mitsuba/core/plugin.h>
#include "../bsdfs/ior.h"

#include <boost/algorithm/string.hpp>
#include <ImfRgbaFile.h>
#include <ImfArray.h>
#include <ImfNamespace.h>
#include <OpenEXRConfig.h>
#include <omp.h>
#if defined(WIN32)
#include <mitsuba/core/getopt.h>
#endif
using namespace OPENEXR_IMF_NAMESPACE;
using namespace std;
MTS_NAMESPACE_BEGIN

class NDFVis : public Utility {
public:
	void help() {
		cout << endl;
		cout << "Synopsis: Muliple Bounce Glint Computation" << endl;
		cout << endl;
		cout << "Usage: mtsutil multibounceglint [options] <Scene XML file or PLY file>" << endl;
		cout << "Options/Arguments:" << endl;
		cout << "   -h             Display this help text" << endl << endl;
		cout << "   -n value       Specify the bounce count" << endl << endl;
		cout << "   -m true/false  Use hierarchy for pruning or not" << endl << endl;
		//cout << "Examples:" << endl;
		//cout << "  E.g. to build a tree for the Stanford bunny having a low SAH cost, type " << endl << endl;
		//cout << "  $ mtsutil kdbench -e .9 -l1 -d48 -x100000 data/tests/bunny.ply" << endl << endl;
		//cout << "  To get SAH costs comparable to [Wald and Havran 06], also specify -t15 -i20" << endl << endl;
		//cout << "  The high -x paramer effectively disables Min-Max binning, which " << endl;
		//cout << "  leads to a slower and more memory-intensive build, so don't try" << endl;
		//cout << "  this on a huge model." << endl << endl;
	}
	bool is_valid(const Vector2& projection) {
		return projection.x * projection.x + projection.y * projection.y < 1.0;
	}
	template <class T>
	inline T randUniform() {
		return rand() / (T)RAND_MAX;
	}

	#define PI_DIVIDE_180 0.0174532922

	//vis the NDF from a microfacet with evaluation
	void visNDFMicrofacetEval(const BSDF *bsdf, Sampler * sampler, Vector3 wi, string outputPath, int outputRes, int side)
	{

		const int res = outputRes;
		Array2D<Rgba> ndfData;
		ndfData.resizeErase(res, res);
		Intersection its;
		its.wi = wi;
		for (int i = 0; i < res; i++) //width
		{
			for (int j = 0; j < res; j++) //width
			{
				ndfData[j][i].r = 0;
				ndfData[j][i].g = 0;
				ndfData[j][i].b = 0;
				ndfData[j][i].a = 1.0f;
			}
		}


#pragma omp parallel for
		for (int i = 0; i < res; i++) //width
		{
			for (int j = 0; j < res; j++) //width
			{
				const Vector2 h2 = Vector2(2.0f * i / res - 1.0, 2.0f * j / res - 1.0);
				if (is_valid(h2))
				{
					Vector h = Vector(h2.x, h2.y, sqrt(1.0f - h2.x * h2.x - h2.y * h2.y) * side);
					Vector3 wo = h;// reflect(wi, h);
					BSDFSamplingRecord bRec(its, wo, ERadiance);
					bRec.sampler = sampler;

					/* Evaluate BSDF * cos(theta) */
					Spectrum value(0.0f);
					for (int spp =0; spp < 128; spp++)
					{
						value += bsdf->eval(bRec);
					}
					value /= 128.0f;
					//	Vector h = normalize(wo + wi);
					//   Vector h = wo;
					int jh = (h.y + 1) * res * 0.5;//(h.y + 1) * res * 0.5;
					int ih = (h.x + 1) * res * 0.5;//(h.x + 1) * res * 0.5;

					ndfData[jh][ih].r = value[0];
					ndfData[jh][ih].g = value[1];
					ndfData[jh][ih].b = value[2];
					ndfData[jh][ih].a = 1.0f;
				}
			}
		}

		float sum = 0.0f;
		for (int i = 0; i < res; i++) //width
		{
			for (int j = 0; j < res; j++) //width
			{
				//	int i = res / 2; int j = res / 2;
				const Vector2 h = Vector2(2.0f * i / res - 1.0, 2.0f * j / res - 1.0);// 1.0 - 2.0f * j / res);
				if (!is_valid(h))
				{
					ndfData[j][i].r = 0.5f;
					ndfData[j][i].g = 0.5f;
					ndfData[j][i].b = 0.5f;
					ndfData[j][i].a = 1.0f;
					continue;
				}
				else
					sum += ndfData[j][i].r;
			}
		}
		float dw = 4.0f / (res*res);
		sum *= dw;
		cout << "Energy is " << sum << endl;
		cout << "Start ouput " << endl;
		RgbaOutputFile file(outputPath.c_str(), res, res, WRITE_RGBA); // 1
		file.setFrameBuffer(&ndfData[0][0], 1, res); // 2
		file.writePixels(res); // 3
		cout << "Output Done " << endl;

	}

	void outputNDF(string output, int res, int *inds, int sampleCount, int validCount)
	{
		int npix = res * res;
		int *bins = new int[npix];
		std::memset(bins, 0, npix * sizeof(int));
		for (int i = 0; i < sampleCount; i++)
		{
			if (inds[i] != -1)
				bins[inds[i]]++;
		}

		Array2D<Rgba> ndfData;
		ndfData.resizeErase(res, res);

		Float scale = Float(npix) / (4 * validCount);
		for (int i = 0; i < npix; i++)
		{
			ndfData[i % res][i /res].r = scale * bins[i];
			ndfData[i % res][i /res].g = scale * bins[i];
			ndfData[i % res][i /res].b = scale * bins[i];
			ndfData[i % res][i /res].a = 1.0f;
		}

		delete[] inds;
		delete[] bins;

		for (int i = 0; i < res; i++) //width
		{
			for (int j = 0; j < res; j++) //width
			{
				const Vector2 h = Vector2(2.0f * i / res - 1.0, 2.0f * j / res - 1.0);// 1.0 - 2.0f * j / res);
				if (!is_valid(h))
				{
					ndfData[j][i].r = 0.5f;
					ndfData[j][i].g = 0.5f;
					ndfData[j][i].b = 0.5f;
					ndfData[j][i].a = 1.0f;
					continue;
				}
			}
		}

		cout << "Start ouput " << endl;
		RgbaOutputFile file(output.c_str(), res, res, WRITE_RGBA); // 1
		file.setFrameBuffer(&ndfData[0][0], 1, res); // 2
		file.writePixels(res); // 3
		cout << "Output Done " << endl;
	}

	void outputNDFValues(string output, int res, int *inds, float *values, int sampleCount, int validCount)
	{
		int npix = res * res;
		int *bins = new int[npix];
		float *valueBin = new float[npix * 3];
		std::memset(bins, 0, npix * sizeof(int));
		std::memset(valueBin, 0, npix * sizeof(float) * 3);
		for (int i = 0; i < sampleCount; i++)
		{
			if (inds[i] != -1)
			{
				valueBin[inds[i] * 3 + 0] += values[i * 3 + 0];
				valueBin[inds[i] * 3 + 1] += values[i * 3 + 1];
				valueBin[inds[i] * 3 + 2] += values[i * 3 + 2];
				bins[inds[i]]++;
			}
		}

		Array2D<Rgba> ndfData;
		ndfData.resizeErase(res, res);

		Float scale = 1;// Float(npix);// / (4 * validCount);
		for (int i = 0; i < npix; i++)
		{
			if (bins[i] != 0)
			{
				ndfData[i % res][i / res].r = scale * valueBin[i * 3 + 0] / bins[i];// bins[i];
				ndfData[i % res][i / res].g = scale * valueBin[i * 3 + 1] / bins[i];
				ndfData[i % res][i / res].b = scale * valueBin[i * 3 + 2] / bins[i];
				ndfData[i % res][i / res].a = 1.0f;
			}
		}

		delete[] inds;
		delete[] bins;
		delete[] valueBin;
		delete[] values;

		for (int i = 0; i < res; i++) //width
		{
			for (int j = 0; j < res; j++) //width
			{
				const Vector2 h = Vector2(2.0f * i / res - 1.0, 2.0f * j / res - 1.0);// 1.0 - 2.0f * j / res);
				if (!is_valid(h))
				{
					ndfData[j][i].r = 0.5f;
					ndfData[j][i].g = 0.5f;
					ndfData[j][i].b = 0.5f;
					ndfData[j][i].a = 1.0f;
					continue;
				}
			}
		}

		cout << "Start ouput " << endl;
		RgbaOutputFile file(output.c_str(), res, res, WRITE_RGBA); // 1
		file.setFrameBuffer(&ndfData[0][0], 1, res); // 2
		file.writePixels(res); // 3
		cout << "Output Done " << endl;
	}


		void print(const Vector3 &dir)
		{
			cout << dir.x << "," << dir.y << "," << dir.z << endl;
		}


		void print(const Point3 &dir)
		{
			cout << dir.x << "," << dir.y << "," << dir.z << endl;
		}
	Vector degree2Vector(const float step, float theta, float phi)
	{
		float offset = 0.5;
		int uI = theta / step;
		int vI = phi / step;
		float theta2 = (uI + offset)* step * PI_DIVIDE_180;
		float cosTheta2 = cos(theta2);

		float dPhi = (vI + offset)* step * PI_DIVIDE_180;
		float sinTheta2 = sqrt(1 - cosTheta2 * cosTheta2);
		return Vector(sinTheta2* cos(dPhi), sinTheta2 * sin(dPhi), cosTheta2);
	}


	//vis NDF from microgeometry, normal map and microfacet
	int run(int argc, char **argv) {
		int optchar;
		optind = 1;
		char *end_ptr = NULL;
		int type = 2;
		int bounce = 1;
		bool merge = false;
	
		type = std::stoi(argv[optind]);
		optind++;

		std::string ndfFile = std::string(argv[optind]);
		optind++;
		ref<FileResolver> fileResolver = Thread::getThread()->getFileResolver();
		//for microfacet, we need a xml for the BSDF input

		ref<Scene> scene;
		ref<ShapeKDTree> kdtree;

		std::string lowercase = boost::to_lower_copy(std::string(argv[optind]));
		if (boost::ends_with(lowercase, ".xml")) {
			fs::path
				filename = fileResolver->resolve(argv[optind]),
				filePath = fs::absolute(filename).parent_path(),
				baseName = filename.stem();
			ref<FileResolver> frClone = fileResolver->clone();
			frClone->prependPath(filePath);
			Thread::getThread()->setFileResolver(frClone);
			scene = loadScene(argv[optind]);
			kdtree = scene->getKDTree();
		}
		else if (boost::ends_with(lowercase, ".ply")) {
			Properties props("ply");
			props.setString("filename", argv[optind]);
			ref<TriMesh> mesh;
			mesh = static_cast<TriMesh *> (PluginManager::getInstance()->
				createObject(MTS_CLASS(TriMesh), props));
			mesh->configure();
			kdtree = new ShapeKDTree();
			kdtree->addShape(mesh);
		}
		else {
			Log(EError, "The supplied scene filename must end in either PLY or XML!");
		}

		scene->initialize();

		//now we have the scene
		optind++;
		float theta_i = std::stof(argv[optind]);
		optind++;
		float cosTheta2 = cos(theta_i);

		float dPhi = M_PI / 2;
		float sinTheta2 = sqrt(1 - cosTheta2 * cosTheta2);
		Vector wi = Vector(sinTheta2* cos(dPhi), sinTheta2 * sin(dPhi), cosTheta2);
		print(wi);

		//Vector3 wi = Vector(0, 0, 1);// Vector(0, 0.6, 0.8);//
		// degree2Vector(10, 30, 90); // 
		if (type == 5)
		{
			//int side = std::stoi(argv[optind]);
			//optind++;
			BSDF *bsdf = scene->getShapes()[0]->getBSDF();
			Sampler *sampler = scene->getSampler();


			//visNDFMicrofacetEvalSphere(bsdf, sampler, wi, ndfFile, 1);
			visNDFMicrofacetEval(bsdf, sampler, wi, ndfFile, 512, 1);
			if (bsdf->hasComponent(BSDF::EGlossyTransmission))
			{
				std::string ndfFile1 = std::string(argv[optind]);
				optind++;
				visNDFMicrofacetEval(bsdf, sampler, wi, ndfFile1, 512, -1);
			}
		}
		else
		{
			cout << "Wrong type.\n";
			return 0;
		}


		return 0;
	}

	MTS_DECLARE_UTILITY()
};

MTS_EXPORT_UTILITY(NDFVis, "NDFVis")
MTS_NAMESPACE_END