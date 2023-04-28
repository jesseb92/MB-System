#ifndef COMPRESSION_H
#define COMPRESSION_H

// local includes
#include "matrix.h"
#include "options.h"

namespace stoqs
{
	namespace compression
	{
		Matrix<float> compress(const Matrix<float>& altitudes, const Options& options);
	}
}

#endif
