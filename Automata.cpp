
#include"Automata.h"

void Automata::getInitSal(const cv::Mat& src, InitValue& initVal, cv::Mat_<float>& initSal)
{
	assert(src.size().area() == initVal.m_info.sz_&&src.type() == CV_32F);

	const int N = initVal.m_info.numlabels_;
	initSal = cv::Mat_<float>(N, 1);
	
	for (int i = 0; i < N; i++)
	{
		float s = 0;
		for (auto j : initVal.m_info.sps_[i])
		{
			s += src.at<float>(j.y, j.x);
		}
		s /= initVal.m_info.features_[i].size_;
		initSal(i) = s;
	}
	return;
}

cv::Mat_<float> Automata::getImpactM(InitValue& initVal)
{
	initVal.getNeighborCnt();
	float valScale(-10.f);
	float eps(1e-8);
	const int hei = initVal.m_info.height_;
	const int wid = initVal.m_info.width_;
	const int N = initVal.m_info.numlabels_;
	cv::Mat_<float> impactM = cv::Mat_<float>::zeros(N, N);
	for (int j = 0; j < N; j++)//first row to last row
	{
		for (int i = 0; i < j; i++)
		{
			impactM(j, i) = impactM(i, j);
		}
		for (auto inb : initVal.m_info.features_[j].neighborCnt_)
		{
			if (inb > j)
			{
				cv::Vec3f cdiff = initVal.m_info.features_[j].mean_normlab_ - initVal.m_info.features_[inb].mean_normlab_;
				impactM(j, inb) = sqrt(cdiff.dot(cdiff));
			}
		}
	}

	cv::normalize(impactM, impactM, 0.0, 1.0, NORM_MINMAX);
	cv::Mat msk = impactM < eps;
	cv::exp(impactM*valScale, impactM);
	cv::subtract(impactM, impactM, impactM, msk);

	//calculate a row - normalized impact factor matrix
	cv::Mat D_sum;
	cv::reduce(impactM, D_sum, 1, CV_REDUCE_SUM);//reduce a column, sum of each row

	D_sum = 1.f / D_sum;
	cv::Mat D = cv::Mat::diag(D_sum);

	cv::Mat re = D * impactM;
	impactM = re;

	return impactM;
}

cv::Mat_<float> Automata::getCoherenceM(const cv::Mat_<float>& impactM)
{
	float a = 0.6;
	float b = 0.2; //[0.2 0.8]
	//cv::Mat_<float> coherenceM(impactM.size());
	cv::Mat C;
	cv::reduce(impactM, C, 1, CV_REDUCE_MAX);//max of each row
	C = 1.0 / C;
	cv::normalize(C, C, b, a + b, NORM_MINMAX);
	cv::Mat coherenceM = cv::Mat::diag(C);
	return coherenceM;
}

void Automata::work(const cv::Mat& src, InitValue& initVal, cv::Mat& dst)
{
	assert(src.size().area() == initVal.m_info.sz_&&src.type() == CV_32F);

	const int N = initVal.m_info.numlabels_;

	cv::Mat_<float> initSal;
	getInitSal(src, initVal, initSal);

	cv::Mat_<float> impactM = getImpactM(initVal);
	cv::Mat_<float> coherenceM = getCoherenceM(impactM);

	inference(initVal, impactM, coherenceM, initSal);

	dst = cv::Mat(src.size(), src.type());
	for (int i = 0; i < N; i++)
	{
		for (auto pt : initVal.m_info.sps_[i])
		{
			dst.at<float>(pt.y, pt.x) = initSal(i);
		}
	}
	return;
}

void Automata::inference(const InitValue& initVal, const cv::Mat_<float>& F_normal,
	const cv::Mat_<float>& C_normal, cv::Mat_<float>& initSal)
{
	const int N = initVal.m_info.numlabels_;
	assert(initSal.rows == N && initSal.cols == 1);
	////%%---------------- - Single - layer Cellular Automata-------------- - %%
	cv::normalize(initSal, initSal, 0.0, 1.0, NORM_MINMAX);

	const cv::Mat_<float> Ident = cv::Mat_<float>::eye(N, N);

	cv::Mat msk0 = cv::Mat::zeros(N, 1, CV_8U);
	for (auto i : initVal.m_borderIdx)
	{
		msk0.at<unsigned char>(i) = 255;
	}
	const cv::Mat& msk(msk0);

	//% step1: decrease the saliency value of boundary superpixels
	for (int lap = 0; lap < 5; lap++)
	{
		for (auto i : initVal.m_borderIdx)
		{
			initSal(i) = max(initSal(i) - 0.6, 0.001);
		}
		cv::Mat inittmp = C_normal * initSal + (1 - C_normal).mul(Ident) * F_normal * initSal;
		initSal = inittmp;
		cv::Mat_<float> ti = initSal.clone();
		cv::subtract(initSal, ti, initSal, msk);
		cv::normalize(initSal, initSal, 0.0, 1.0, NORM_MINMAX);
		cv::add(initSal, ti, initSal, msk);
	}

	//% step2: control the ratio of foreground larger than a threshold
	for (int lap = 0; lap < 5; lap++)
	{
		for (auto i : initVal.m_borderIdx)
		{
			initSal(i) = max(initSal(i) - 0.6, 0.001);
		}
		cv::Mat fi = initSal > 0.93;
		float numel = cv::sum(fi / 255)[0];
		if (numel < 0.02*N)
		{
			cv::Mat_<float> ti = initSal.clone();
			cv::subtract(initSal, ti, initSal, fi);
			cv::normalize(initSal, initSal, 0.0, 1.0, NORM_MINMAX);
			cv::add(initSal, ti, initSal, fi);
		}
		cv::Mat inittmp = C_normal * initSal + (1 - C_normal).mul(Ident) * F_normal * initSal;
		initSal = inittmp;
		cv::Mat_<float> ti = initSal.clone();
		cv::subtract(initSal, ti, initSal, msk);
		cv::normalize(initSal, initSal, 0.0, 1.0, NORM_MINMAX);
		cv::add(initSal, ti, initSal, msk);
	}

	//% step3: simply update the saliency map according to rules
	for (int lap = 0; lap < 10; lap++)
	{
		cv::Mat inittmp = C_normal * initSal + (1 - C_normal).mul(Ident) * F_normal * initSal;
		initSal = inittmp;
		cv::normalize(initSal, initSal, 0.0, 1.0, NORM_MINMAX);
	}
	return;
}