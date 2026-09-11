#pragma once
#include "Vector.hpp"

/**
 * Matrix 4x4
 * 0, 4, 8, 12,
 * 1, 5, 9, 13,
 * 2, 6,10, 14,
 * 3, 7,11, 15
 *
 * M00 M10 M20 M30
 * M01 M11 M21 M31
 * M02 M12 M22 M32
 * M03 M13 M23 M33
 *
 */
template<typename T>
struct alignas(16) TMatrix4 {
    static_assert(std::is_floating_point<T>::value);
#if defined(SIMD_SUPPORTED_NEON)
    using DataType = std::conditional_t<std::is_same_v<T, float>, float32x4_t, float64x1x4_t>;
#elif defined(SIMD_SUPPORTED)
    using DataType = std::conditional_t<std::is_same_v<T, float>, __m128, __m256d>;
#endif
    
public:
	union {
		alignas(16) T data[16] = { 0.0f };
	};

	TMatrix4() {
		Memory::Zero(data, sizeof(T) * 16);
	}

	TMatrix4(const std::vector<T>& d) {
		if (d.size() == 16) {
			for (size_t i = 0; i < d.size(); ++i) {
				data[i] = static_cast<T>(d[i]);
			}
		}

		ASSERT(d.size() == 16);
	}

	TMatrix4(T d1,  T d2,  T d3,  T d4, 
			 T d5,  T d6,  T d7,  T d8, 
			 T d9,  T d10, T d11, T d12, 
			 T d13, T d14, T d15, T d16) {
		data[0]  = d1;  data[4]  = d2;  data[8]  = d3;  data[12]  = d4;
		data[1]  = d5;  data[5]  = d6;  data[9]  = d7;  data[13]  = d8;
		data[2]  = d9;  data[6]  = d10; data[10] = d11; data[14] = d12;
		data[3]  = d13; data[7]  = d14; data[11] = d15; data[15] = d16;
	}

	/*
	* @brief Returns the result of multiplying 
	* M2 * M1
	*
	* @param mat The matrix to be multiplied.
	* @return The result of the matrix multiplication.
	*/
	TMatrix4 Multiply(const TMatrix4& mat) const {
		const T* MatPtr1 = mat.data;
		const T* MatPtr2 = data;

		TMatrix4 NewMat = TMatrix4::Identity();
		T* DstPtr = NewMat.data;

#if defined(SIMD_SUPPORTED_NEON)
    for (int i = 0; i < 4; ++i) {
        // Load Mat1's i-th row (column-major access)
        float32x4_t row1 = vld1q_f32(&MatPtr1[i * 4]);

        for (int j = 0; j < 4; j++) {
            // Load Mat2's j-th column (column-major access)
            float32x4_t col2 = { MatPtr2[j], MatPtr2[4 + j], MatPtr2[8 + j], MatPtr2[12 + j] };

            // Perform element-wise multiplication and horizontal addition
            float32x4_t product = vmulq_f32(row1, col2);

            // Horizontal addition to accumulate the results
            float32x2_t sum_pair = vadd_f32(vget_low_f32(product), vget_high_f32(product)); // Add pairs
            float32x2_t sum_final = vpadd_f32(sum_pair, sum_pair); // Final horizontal addition

            // Store the resulting scalar into the matrix
            DstPtr[i * 4 + j] = vget_lane_f32(sum_final, 0); // Extract the final sum
        }
    }
#elif defined(SIMD_SUPPORTED)
		for (int i = 0; i < 4; ++i) {
			// 加载Mat1的第i列（按列主序，逐列访问）
			DataType row1 = _mm_set_ps(MatPtr1[i * 4 + 3], MatPtr1[i * 4 + 2], MatPtr1[i * 4 + 1], MatPtr1[i * 4 + 0]);
			for (int j = 0; j < 4; j++) {
				// 加载Mat2的第j列（按列主序，逐列访问）
				DataType col2 = _mm_set_ps(MatPtr2[12 + j], MatPtr2[8 + j], MatPtr2[4 + j], MatPtr2[0 + j]);

				// 执行乘法并累加
				DataType result = _mm_mul_ps(row1, col2);
				result = _mm_hadd_ps(result, result);  // 水平加法：先加前两对元素，再加后两对元素
				result = _mm_hadd_ps(result, result);  // 再加一次

				// 将结果存储回目标矩阵
				T FinalRest = _mm_cvtss_f32(result);		// result[0]
				DstPtr[i * 4 + j] = FinalRest;
			}
        }
#else
		for (int i = 0; i < 4; ++i) {
			for (int j = 0; j < 4; j++) {
				*DstPtr = MatPtr1[i * 4 + 0] * MatPtr2[0 + j] +
					MatPtr1[i * 4 + 1] * MatPtr2[4 + j] +
					MatPtr1[i * 4 + 2] * MatPtr2[8 + j] +
					MatPtr1[i * 4 + 3] * MatPtr2[12 + j];
				DstPtr++;
			}
		}
#endif

		return NewMat;
	}

	TMatrix4 Transpose() const {
		TMatrix4 result = *this;
		for (int row = 0; row < 4; ++row) {
			for (int col = row + 1; col < 4; ++col) {
				std::swap(result.data[row * 4 + col], result.data[col * 4 + row]);
			}
		}
		return result;
	}

	/*
	* @brief Create and returns an inverse of the provided matrix.
	*
	* @return A inverted copy of the matrix.
	*/
	TMatrix4 Inverse() const {
		float t0 = data[10] * data[15];
		float t1 = data[14] * data[11];
		float t2 = data[6] * data[15];
		float t3 = data[14] * data[7];
		float t4 = data[6] * data[11];
		float t5 = data[10] * data[7];
		float t6 = data[2] * data[15];
		float t7 = data[14] * data[3];
		float t8 = data[2] * data[11];
		float t9 = data[10] * data[3];
		float t10 = data[2] * data[7];
		float t11 = data[6] * data[3];
		float t12 = data[8] * data[13];
		float t13 = data[12] * data[9];
		float t14 = data[4] * data[13];
		float t15 = data[12] * data[5];
		float t16 = data[4] * data[9];
		float t17 = data[8] * data[5];
		float t18 = data[0] * data[13];
		float t19 = data[12] * data[1];
		float t20 = data[0] * data[9];
		float t21 = data[8] * data[1];
		float t22 = data[0] * data[5];
		float t23 = data[4] * data[1];

		TMatrix4 Matrix;
		float* o = Matrix.data;

		o[0] = (t0 * data[5] + t3 * data[9] + t4 * data[13]) - (t1 * data[5] + t2 * data[9] + t5 * data[13]);
		o[1] = (t1 * data[1] + t6 * data[9] + t9 * data[13]) - (t0 * data[1] + t7 * data[9] + t8 * data[13]);
		o[2] = (t2 * data[1] + t7 * data[5] + t10 * data[13]) - (t3 * data[1] + t6 * data[5] + t11 * data[13]);
		o[3] = (t5 * data[1] + t8 * data[5] + t11 * data[9]) - (t4 * data[1] + t9 * data[5] + t10 * data[9]);

		float d = 1.0f / (data[0] * o[0] + data[4] * o[1] + data[8] * o[2] + data[12] * o[3]);

		o[0] = d * o[0];
		o[1] = d * o[1];
		o[2] = d * o[2];
		o[3] = d * o[3];

		o[4] = d * ((t1 * data[4] + t2 * data[8] + t5 * data[12]) - (t0 * data[4] + t3 * data[8] + t4 * data[12]));
		o[5] = d * ((t0 * data[0] + t7 * data[8] + t8 * data[12]) - (t1 * data[0] + t6 * data[8] + t9 * data[12]));
		o[6] = d * ((t3 * data[0] + t6 * data[4] + t11 * data[12]) - (t2 * data[0] + t7 * data[4] + t10 * data[12]));
		o[7] = d * ((t4 * data[0] + t9 * data[4] + t10 * data[8]) - (t5 * data[0] + t8 * data[4] + t11 * data[8]));

		o[8] = d * ((t12 * data[7] + t15 * data[11] + t16 * data[15]) - (t13 * data[7] + t14 * data[11] + t17 * data[15]));
		o[9] = d * ((t13 * data[3] + t18 * data[11] + t21 * data[15]) - (t12 * data[3] + t19 * data[11] + t20 * data[15]));
		o[10] = d * ((t14 * data[3] + t19 * data[7] + t22 * data[15]) - (t15 * data[3] + t18 * data[7] + t23 * data[15]));
		o[11] = d * ((t17 * data[3] + t20 * data[7] + t23 * data[11]) - (t16 * data[3] + t21 * data[7] + t22 * data[11]));

		o[12] = d * ((t14 * data[10] + t17 * data[14] + t13 * data[6]) - (t16 * data[14] + t12 * data[6] + t15 * data[10]));
		o[13] = d * ((t20 * data[14] + t12 * data[2] + t19 * data[10]) - (t18 * data[10] + t21 * data[14] + t13 * data[2]));
		o[14] = d * ((t18 * data[6] + t23 * data[14] + t15 * data[2]) - (t22 * data[14] + t14 * data[2] + t19 * data[6]));
		o[15] = d * ((t22 * data[10] + t16 * data[2] + t21 * data[6]) - (t20 * data[6] + t23 * data[10] + t17 * data[2]));

		return Matrix;
	}

	void SetTranslation(const TVector3<T>& position) {
		data[12] = position.x;
		data[13] = position.y;
		data[14] = position.z;
	}

	TVector3<T> GetTranslation() const {
		return TVector3<T>{ data[12], data[13], data[14] };
	}

	void SetScale(const TVector3<T>& scale) {
		data[0] = scale.x;
		data[5] = scale.y;
		data[10] = scale.z;
	}

	TVector3<T> GetScale() const {
		return TVector3<T>{ data[0], data[5], data[10] };
	}

	/*
	* @brief Returns a forward vector relative to the matrix.
	*
	* @return A 3-Component directional vector.
	*/
	TVector3<T> Forward() const {
		TVector3<T> Vector;
		Vector.x = -data[2];
		Vector.y = -data[6];
		Vector.z = -data[10];
		return Vector.Normalized();
	}

	/*
	* @brief Returns a backward vector relative to the matrix.
	*
	* @return A 3-component directional vector.
	*/
	TVector3<T> Backward() const {
		TVector3<T> Vector;
		Vector.x = data[2];
		Vector.y = data[6];
		Vector.z = data[10];
		return Vector.Normalized();
	}

	/*
	* @brief Returns a upward vector relative to the matrix
	*
	* @return A 3-component directional vector
	*/
	TVector3<T> Up() const {
		TVector3<T> Vector;
		Vector.x = data[1];
		Vector.y = data[5];
		Vector.z = data[9];
		return Vector.Normalized();
	}

	/*
	* @brief Returns a down vector relative to the matrix
	*
	* @return A 3-component directional vector
	*/
	TVector3<T> Down() const {
		TVector3<T> Vector;
		Vector.x = -data[1];
		Vector.y = -data[5];
		Vector.z = -data[9];
		return Vector.Normalized();
	}

	/*
	* @brief Returns a left vector relative to the matrix
	*
	* @return A 3-component directional vector
	*/
	TVector3<T> Left() const {
		TVector3<T> Vector;
		Vector.x = -data[0];
		Vector.y = -data[4];
		Vector.z = -data[8];
		return Vector.Normalized();
	}

	/*
	* @brief Returns a right vector relative to the matrix
	*
	* @return A 3-component directional vector
	*/
	TVector3<T> Right() const {
		TVector3<T> Vector;
		Vector.x = data[0];
		Vector.y = data[4];
		Vector.z = data[8];
		return Vector.Normalized();
	}

public:
	/*
	* @brief Generate Identity Matrix
	*
	* @return Identity matrix
	* { 1, 0, 0, 0,
	*   0, 1, 0, 0,
	*   0, 0, 1, 0,
	*   0, 0, 0, 1}
	*/
	static TMatrix4 Identity() {
		TMatrix4 Mat;
		Mat.data[0] = 1.0f;
		Mat.data[5] = 1.0f;
		Mat.data[10] = 1.0f;
		Mat.data[15] = 1.0f;
		return Mat;
	}

	/*
	* @brief Creates and return s an orthographic projection matrix. Typically used to render flat or 2D scene.
	*
	* @param left The left side of the view frustum.
	* @param right The right side of the view frustum.
	* @param bottom The bottom side of the view frustum.
	* @param top The top side of the view frustum.
	* @param near_clip The near clipping plane distance.
	* @param far_clip The far clipping plane distance.
	* @param zero_to_one 为 true 时按 Vulkan/D3D 约定把 z 映射到 [0, 1]（近平面为 0、远平面为 1），
	*        用于渲染目标/阴影贴图这类直接与光栅化深度比较的矩阵；默认 false 保持原 OpenGL 风格 [-1, 1]。
	* @return A new orthographic projection matrix.
	*/
	static TMatrix4 Orthographic(float left, float right, float bottom, float top, float near_clip, float far_clip, bool reverse_y = false, bool zero_to_one = false) {
		TMatrix4<T> Matrix = TMatrix4<T>::Identity();

		float lr = 1.0f / (left - right);
		float bt = 1.0f / (bottom - top);
		float nf = 1.0f / (near_clip - far_clip);

		Matrix.data[0] = -2.0f * lr;
		Matrix.data[5] = -2.0f * bt;

		if (zero_to_one) {
			// Vulkan 裁剪体 z ∈ [0, w]，OpenGL 风格的 2*nf / (far+near)*nf 会把近平面映射到 -1，
			// 导致整个场景落在裁剪体之外（深度通道什么都写不进去，阴影贴图保持 clear 值）。
			Matrix.data[10] = nf;                 // -1 / (far - near)
			Matrix.data[14] = near_clip * nf;     // -near / (far - near)
		}
		else {
			Matrix.data[10] = 2.0f * nf;
			Matrix.data[14] = (far_clip + near_clip) * nf;
		}

		Matrix.data[12] = (left + right) * lr;
		Matrix.data[13] = (top + bottom) * bt;

		if (reverse_y) {
			Matrix.data[5] *= -1.0f;
		}

		return Matrix;
	}

	/*
	* @brief Creates and returns a perspective matrix. Typically used to render 3D scenes.
	*
	* @param fov_radians The field of view in radians.
	* @param aspect_ratio The aspect ratio.
	* @param near_clip The near clipping plane distance.
	* @param far_clip The far clipping plane distance.
	* @return A new perspective matrix.
	*/
	static TMatrix4 Perspective(float fov_radians, float aspect_ratio, float near_clip, float far_clip, bool reverse_y = false) {
		float HalfFov = DTan(fov_radians * 0.5f);

		TMatrix4 Matrix;
		Memory::Zero(Matrix.data, sizeof(float) * 16);

		Matrix.data[0] = 1.0f / (aspect_ratio * HalfFov);
		Matrix.data[5] = 1.0f / HalfFov;
		Matrix.data[10] = (near_clip + far_clip) / (near_clip - far_clip);
		Matrix.data[11] = -1.0f;
		Matrix.data[14] = (2.0f * far_clip * near_clip) / (near_clip - far_clip);

		if (reverse_y) {
			Matrix.data[5] *= -1.0f;
		}

		return Matrix;
	}

	static TMatrix4 LookAt(const TVector3<T>& position, const TVector3<T>& target, const TVector3<T>& up, bool isRH = true) {
		if (isRH) {
			return LookAtRH(position, target, up);
		}
		else {
			return LookAtLH(position, target, up);
		}
	}

	/*
	* @brief Creates and returns a look-at matrix, or a matrix looking at target from the perspective of position.
	* Look forward -Z
	*
	* @param position The position of the matrix.
	* @param target The look at target.
	* @param up The up vector.
	* @return A matrix looking at target from the perspective of position.
	*/
	static TMatrix4 LookAtRH(const TVector3<T>& position, const TVector3<T>& target, const TVector3<T>& up) {
		TMatrix4 Matrix = TMatrix4::Identity();
		if (position.Compare(target)) {
			return Matrix;
		}

		TVector3<T> AxisZ = (position - target).Normalized();	
		TVector3<T> AxisX = AxisZ.Cross(up);
		TVector3<T> AxisY = AxisX.Cross(AxisZ);

		Matrix.data[0] = AxisX.x;
		Matrix.data[4] = AxisY.x;
		Matrix.data[8] = AxisZ.x;	

		Matrix.data[1] = AxisX.y;
		Matrix.data[5] = AxisY.y;
		Matrix.data[9] = AxisZ.y;

		Matrix.data[2] = AxisX.z;
		Matrix.data[6] = AxisY.z;
		Matrix.data[10] = AxisZ.z;

		Matrix.data[12] = -position.Dot(AxisX);		// Local axis
		Matrix.data[13] = -position.Dot(AxisY);		// Local axis
		Matrix.data[14] = -position.Dot(AxisZ);		// Local axis
		//Matrix.data[12] = -position.x;		// World axis
		//Matrix.data[13] = -position.y;		// World axis
		//Matrix.data[14] = -position.z;		// World axis

		return Matrix;
	}
	/*
	* @brief Creates and returns a look-at matrix, or a matrix looking at target from the perspective of position.
	* Look forward -Z
	*
	* @param position The position of the matrix.
	* @param target The look at target.
	* @param up The up vector.
	* @return A matrix looking at target from the perspective of position.
	*/
	static TMatrix4 LookAtLH(const TVector3<T>& position, const TVector3<T>& target, const TVector3<T>& up) {
		GLOG(Log::eWarn, "Not support LookAtLH yet!");
		return TMatrix4();
	}


	static TMatrix4 FromTranslation(const TVector3<T>& trans) {
		TMatrix4 Ret = TMatrix4::Identity();
		Ret.SetTranslation(trans);
		return Ret;
	}

	static TMatrix4 FromScale(const TVector3<T>& scale) {
		TMatrix4 Ret = TMatrix4::Identity();
		Ret.SetScale(scale);
		return Ret;
	}

	/*
	* | 1      0       0  |
	* | 0    cosp	-sinp |
	* | 0    sinp    cosp |
	*/
	static TMatrix4 EulerX(T angle_radians) {
		TMatrix4 Matrix = TMatrix4::Identity();

		float c = DCos(angle_radians);
		float s = DSin(angle_radians);

		Matrix.data[5] = c;
		Matrix.data[6] = s;
		Matrix.data[9] = -s;
		Matrix.data[10] = c;

		return Matrix;
	}

	/**
	* | cosy    0    siny |
	* |   0     1     0   |
	* | -siny   0    cosy |
	*/
	static TMatrix4 EulerY(T angle_radians) {
		TMatrix4 Matrix = TMatrix4::Identity();
		float c = DCos(angle_radians);
		float s = DSin(angle_radians);

		Matrix.data[0] = c;
		Matrix.data[2] = -s;
		Matrix.data[8] = s;
		Matrix.data[10] = c;

		return Matrix;
	}

	/**
	* | cosr   -sinr    0 |
	* | sinr    cosr    0 |
	* |   0      0      1 |
	*/
	static TMatrix4 EulerZ(T angle_radians) {
		TMatrix4 Matrix = TMatrix4::Identity();
		float c = DCos(angle_radians);
		float s = DSin(angle_radians);

		Matrix.data[0] = c;
		Matrix.data[1] = s;
		Matrix.data[4] = -s;
		Matrix.data[5] = c;

		return Matrix;
	}

	/**
	 * @brief Rotation Z - Y - X
	 */
	static TMatrix4 EulerXYZ(T x_radians, T y_radians, T z_radians) {
		TMatrix4 Matrix = TMatrix4::Identity();
		TMatrix4 mx = TMatrix4::EulerX(x_radians);
		TMatrix4 my = TMatrix4::EulerY(y_radians);
		TMatrix4 mz = TMatrix4::EulerZ(z_radians);

		Matrix = mz.Multiply(my.Multiply(mx));

		return Matrix;
	}

public:
	TMatrix4& operator=(const TMatrix4& mat) {
		data[0] = mat.data[0];
		data[1] = mat.data[1];
		data[2] = mat.data[2];
		data[3] = mat.data[3];
		data[4] = mat.data[4];
		data[5] = mat.data[5];
		data[6] = mat.data[6];
		data[7] = mat.data[7];
		data[8] = mat.data[8];
		data[9] = mat.data[9];
		data[10] = mat.data[10];
		data[11] = mat.data[11];
		data[12] = mat.data[12];
		data[13] = mat.data[13];
		data[14] = mat.data[14];
		data[15] = mat.data[15];

		return *this;
	}

    bool operator==(const TMatrix4& other) {
            for (int i = 0; i < 16; ++i) {
                if (Dabs(other.data[i] - data[i]) > D_FLOAT_EPSILON) {
                    return false;
                }
            }
            return true;
        }

        bool operator!=(const TMatrix4& other) {
            return (!(*this == other));
        }

        template<typename TypeInex>
        T& operator[](TypeInex i) {
            return data[i];
        }

        template<typename TypeInex>
        const T& operator[](TypeInex i) const {
            return data[i];
        }

        TMatrix4 operator*(const TMatrix4& other) {
            const T* MatPtr1 = other.data;
            const T* MatPtr2 = data;

            TMatrix4 NewMat = TMatrix4::Identity();
            T* DstPtr = NewMat.data;

    #if defined(SIMD_SUPPORTED)
            if constexpr (std::is_same_v<T, float>) {
                for (int i = 0; i < 4; ++i) {
                    // 加载第一个矩阵的第i行
                    typename SIMDHelper<T>::SIMDType row1;
                    T row_data[4] = {MatPtr1[i * 4 + 0], MatPtr1[i * 4 + 1], MatPtr1[i * 4 + 2], MatPtr1[i * 4 + 3]};
                    SIMDHelper<T>::load(row_data, row1);

                    for (int j = 0; j < 4; j++) {
                        // 加载第二个矩阵的第j列
                        T col_data[4] = {MatPtr2[0 + j], MatPtr2[4 + j], MatPtr2[8 + j], MatPtr2[12 + j]};
                        typename SIMDHelper<T>::SIMDType col2;
                        SIMDHelper<T>::load(col_data, col2);

                        // 执行乘法并累加
                        typename SIMDHelper<T>::SIMDType result = SIMDHelper<T>::mul(row1, col2);
                        T final_result = SIMDHelper<T>::horizontal_add(result);
                        
                        DstPtr[i * 4 + j] = final_result;
                    }
                }
            } else {
    #endif
                for (int i = 0; i < 4; ++i) {
                    for (int j = 0; j < 4; j++) {
                        *DstPtr = MatPtr1[i * 4 + 0] * MatPtr2[0 + j] +
                            MatPtr1[i * 4 + 1] * MatPtr2[4 + j] +
                            MatPtr1[i * 4 + 2] * MatPtr2[8 + j] +
                            MatPtr1[i * 4 + 3] * MatPtr2[12 + j];
                        DstPtr++;
                    }
                }
    #if defined(SIMD_SUPPORTED)
            }
    #endif

            return NewMat;
        }

        friend std::ostream& operator<<(std::ostream& os, const TMatrix4& mat) {
            return os
                << mat[0] << " " << mat[4] << " " << mat[8] << " " << mat[12] << "\n"
                << mat[1] << " " << mat[5] << " " << mat[9] << " " << mat[13] << "\n"
                << mat[2] << " " << mat[6] << " " << mat[10] << " " << mat[14] << "\n"
                << mat[3] << " " << mat[7] << " " << mat[11] << " " << mat[15] << "\n";
        }

        /**
         * @brief Performs v * m
         *
         * @param m The matrix to be multiplied.
         * @param v The vector to multiply by.
         * @return The transformed vector.
         */
        friend TVector3<T> operator*(const TVector3<T>& v, const TMatrix4& m) {
    #if defined(SIMD_SUPPORTED)
            if constexpr (std::is_same_v<T, float>) {
                // 准备向量数据，包含齐次坐标
                T vec_data[4] = {v.x, v.y, v.z, 1.0f};
                typename SIMDHelper<T>::SIMDType vec_simd;
                SIMDHelper<T>::load(vec_data, vec_simd);

                T result_data[3];
                for (int i = 0; i < 3; ++i) {
                    // 加载矩阵行
                    typename SIMDHelper<T>::SIMDType row;
                    SIMDHelper<T>::load(&m.data[i * 4], row);
                    
                    // 执行点积
                    typename SIMDHelper<T>::SIMDType mul_result = SIMDHelper<T>::mul(vec_simd, row);
                    result_data[i] = SIMDHelper<T>::horizontal_add(mul_result);
                }

                return TVector3<T>(result_data[0], result_data[1], result_data[2]);
            }
    #endif
            return TVector3<T>(
                v.x * m.data[0] + v.y * m.data[1] + v.z * m.data[2] + m.data[3],
                v.x * m.data[4] + v.y * m.data[5] + v.z * m.data[6] + m.data[7],
                v.x * m.data[8] + v.y * m.data[9] + v.z * m.data[10] + m.data[11]
            );
        }

        /**
         * @brief Performs m * v
         *
         * @param v The vector to be multiplied.
         * @param m The matrix to be multiply by.
         * @return The transformed vector.
         */
        friend TVector3<T> operator*(const TMatrix4& m, const TVector3<T>& v) {
    #if defined(SIMD_SUPPORTED)
            if constexpr (std::is_same_v<T, float>) {
                // 准备向量数据，包含齐次坐标
                T vec_data[4] = {v.x, v.y, v.z, 1.0f};
                typename SIMDHelper<T>::SIMDType vec_simd;
                SIMDHelper<T>::load(vec_data, vec_simd);

                T result_data[3];
                for (int i = 0; i < 3; ++i) {
                    // 加载矩阵列
                    T col_data[4] = {m.data[i], m.data[i + 4], m.data[i + 8], m.data[i + 12]};
                    typename SIMDHelper<T>::SIMDType col;
                    SIMDHelper<T>::load(col_data, col);
                    
                    // 执行点积
                    typename SIMDHelper<T>::SIMDType mul_result = SIMDHelper<T>::mul(vec_simd, col);
                    result_data[i] = SIMDHelper<T>::horizontal_add(mul_result);
                }

                return TVector3<T>(result_data[0], result_data[1], result_data[2]);
            }
    #else
            return TVector3<T>(
                v.x * m.data[0] + v.y * m.data[4] + v.z * m.data[8] + m.data[12],
                v.x * m.data[1] + v.y * m.data[5] + v.z * m.data[9] + m.data[13],
                v.x * m.data[2] + v.y * m.data[6] + v.z * m.data[10] + m.data[14]
            );
	#endif
        }

        /**
         * @brief Performs m * v
         *
         * @param m The matrix to be multiplied.
         * @param v The vector to multiply by.
         * @return The transformed vector.
         */
        friend TVector4<T> operator*(const TMatrix4& m, const TVector4<T>& v) {
    #if defined(SIMD_SUPPORTED)
            if constexpr (std::is_same_v<T, float>) {
                // 准备向量数据
                T vec_data[4] = {v.x, v.y, v.z, v.w};
                typename SIMDHelper<T>::SIMDType vec_simd;
                SIMDHelper<T>::load(vec_data, vec_simd);

                T result_data[4];
                for (int i = 0; i < 4; ++i) {
                    // 加载矩阵行
                    typename SIMDHelper<T>::SIMDType row;
                    SIMDHelper<T>::load(&m.data[i * 4], row);
                    
                    // 执行点积
                    typename SIMDHelper<T>::SIMDType mul_result = SIMDHelper<T>::mul(vec_simd, row);
                    result_data[i] = SIMDHelper<T>::horizontal_add(mul_result);
                }

                return TVector4<T>(result_data[0], result_data[1], result_data[2], result_data[3]);
            }
    #endif
            return TVector4<T>(
                v.x * m.data[0] + v.y * m.data[1] + v.z * m.data[2] + v.w * m.data[3],
                v.x * m.data[4] + v.y * m.data[5] + v.z * m.data[6] + v.w * m.data[7],
                v.x * m.data[8] + v.y * m.data[9] + v.z * m.data[10] + v.w * m.data[11],
                v.x * m.data[12] + v.y * m.data[13] + v.z * m.data[14] + v.w * m.data[15]
            );
        }

        /**
         * @brief Performs v * m
         *
         * @param v The vector to be multiplied.
         * @param m The matrix to be multiply by.
         * @return The transformed vector.
         */
        friend TVector4<T> operator*(const TVector4<T>& v, const TMatrix4& m) {
    #if defined(SIMD_SUPPORTED)
            if constexpr (std::is_same_v<T, float>) {
                // 准备向量数据
                T vec_data[4] = {v.x, v.y, v.z, v.w};
                typename SIMDHelper<T>::SIMDType vec_simd;
                SIMDHelper<T>::load(vec_data, vec_simd);

                T result_data[4];
                for (int i = 0; i < 4; ++i) {
                    // 加载矩阵列
                    T col_data[4] = {m.data[i], m.data[i + 4], m.data[i + 8], m.data[i + 12]};
                    typename SIMDHelper<T>::SIMDType col;
                    SIMDHelper<T>::load(col_data, col);
                    
                    // 执行点积
                    typename SIMDHelper<T>::SIMDType mul_result = SIMDHelper<T>::mul(vec_simd, col);
                    result_data[i] = SIMDHelper<T>::horizontal_add(mul_result);
                }

                return TVector4<T>(result_data[0], result_data[1], result_data[2], result_data[3]);
            }
    #endif
            return TVector4<T>(
                v.x * m.data[0] + v.y * m.data[4] + v.z * m.data[8] + v.w * m.data[12],
                v.x * m.data[1] + v.y * m.data[5] + v.z * m.data[9] + v.w * m.data[13],
                v.x * m.data[2] + v.y * m.data[6] + v.z * m.data[10] + v.w * m.data[14],
                v.x * m.data[3] + v.y * m.data[7] + v.z * m.data[11] + v.w * m.data[15]
            );
        }

        TVector4<T> GetColumn(int Col) const {
            if (Col < 0 || Col > 3) {
                GLOG(Log::eWarn, "Invalid matrix boundings. Return Vec4().");
                return TVector4<T>();
            }

            return TVector4<T>(data[Col], data[Col + 4], data[Col + 8], data[Col + 12]);
        }

        TVector4<T> GetRow(int Row) const {
            if (Row < 0 || Row > 3) {
                GLOG(Log::eWarn, "Invalid matrix boundings. Return Vec4().");
                return TVector4<T>(0.0f);
            }

            return TVector4<T>(data[Row * 4], data[Row * 4 + 1], data[Row * 4 + 2], data[Row * 4 + 3]);
        }

        // 批量矩阵运算函数
        static void BatchMultiply(TMatrix4* results, const TMatrix4* matrices_a, const TMatrix4* matrices_b, size_t count) {
    #if defined(SIMD_SUPPORTED)
            if constexpr (std::is_same_v<T, float>) {
                for (size_t idx = 0; idx < count; ++idx) {
                    results[idx] = matrices_a[idx] * matrices_b[idx];
                }
            } else {
    #endif
                for (size_t idx = 0; idx < count; ++idx) {
                    results[idx] = matrices_a[idx] * matrices_b[idx];
                }
    #if defined(SIMD_SUPPORTED)
            }
    #endif
        }

        // 批量矩阵向量乘法
        static void BatchTransform(TVector4<T>* results, const TMatrix4* matrices, const TVector4<T>* vectors, size_t count) {
    #if defined(SIMD_SUPPORTED)
            if constexpr (std::is_same_v<T, float>) {
                for (size_t idx = 0; idx < count; ++idx) {
                    results[idx] = matrices[idx] * vectors[idx];
                }
            } else {
    #endif
                for (size_t idx = 0; idx < count; ++idx) {
                    results[idx] = matrices[idx] * vectors[idx];
                }
    #if defined(SIMD_SUPPORTED)
            }
    #endif
        }

private:
	void Swap(T* a, T* b) {
		T c = *a;
		*a = *b;
		*b = c;
	}

};
