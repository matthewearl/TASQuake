#include "cpp_quakedef.hpp"
#include "utils.hpp"
#include <cmath>
#include <cstdint>
#include <algorithm>

bool IsZero(double number)
{
	return std::abs(number) < 0.01;
}

double NormalizeRad(double a)
{
	return std::fmod(a, M_PI * 2);
}

double NormalizeDeg(double a)
{
	return std::fmod(a, 360.0);
}

float AngleModDeg(float deg)
{
	int number = static_cast<int>(deg * 65536.0 / 360.0) & 65535;
	return number * (360.0 / 65536);
}

// Algorithm from http://esrg.sourceforge.net/docs/paper_brap_reduced.pdf
void BrapAlg(double& outInteger1, double& outInteger2, int64_t max_int)
{
	double ratio = outInteger1 / outInteger2;
	int64_t p[3], q[3];
	int64_t remainder = 1e18;
	int64_t divisor = std::round(remainder * ratio);
	p[0] = 1;
	p[1] = 0;
	p[2] = 0;
	q[0] = 0;
	q[1] = 0;
	q[2] = 0;


	for (int i = 0; remainder != 0 && q[0] <= max_int; ++i)
	{
		p[2] = p[1];
		p[1] = p[0];
		q[2] = q[1];
		q[1] = q[0];

		int64_t dividend = divisor;
		divisor = remainder;
		int64_t a = dividend / divisor;
		remainder = dividend % divisor;
		if (i == 0)
		{
			p[0] = a;
			q[0] = 1;
		}
		else
		{
			p[0] = a * p[1] + p[2];
			q[0] = a * q[1] + q[2];
		}

	}

	if (remainder == 0 && q[0] <= max_int)
	{
		int mult = max_int / std::fmax(q[0], p[0]);
		outInteger1 = p[0] * mult;
		outInteger2 = q[0] * mult;
	}
	else
	{
		int mult = max_int / std::fmax(q[1], p[1]);
		outInteger1 = p[1] * mult;
		outInteger2 = q[1] * mult;
	}
}

void ApproximateRatioWithIntegers(double& number1, double& number2, int max_int)
{
	double ratio = number1 / number2;
	int num1Sign = number1 >= 0 ? 1 : -1;
	int num2Sign = number2 >= 0 ? 1 : -1;
	number1 = std::abs(number1);
	number2 = std::abs(number2);

	bool flip = number1 > number2;
	if(flip)
		std::swap(number1, number2);


	BrapAlg(number1, number2, max_int);

	if (flip)
		std::swap(number1, number2);

	number1 *= num1Sign;
	number2 *= num2Sign;

}