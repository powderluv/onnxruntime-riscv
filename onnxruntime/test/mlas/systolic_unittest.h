#define ABS(x) (x < 0 ? -x : x)
#ifdef SYSTOLIC_INT8
#define ACC_SCALE(x, scale) \
    ({float y = nearbyint((x) * (scale)); y > INT_MAX ? INT_MAX : (y < INT_MIN ? INT_MIN : (acc_t)y);})
#define MVIN_SCALE(x, scale) \
    (scale == 1.0 ? x : \
    ({float y = nearbyint((x) * (scale)); y > INT8_MAX ? INT8_MAX : (y < INT8_MIN ? INT8_MIN : (elem_t)y);}))
#define ELEM_T_MAX SCHAR_MAX
#define ELEM_T_MIN SCHAR_MIN
#define FMT "%d "
#else
#define ACC_SCALE(x, scale) \
    ({float y = (x) * (scale); (acc_t) y;})
#define MVIN_SCALE(x, scale) \
    ({float y = (x) * (scale); (elem_t) y;})
#define ELEM_T_MAX 3.4028235E38
#define ELEM_T_MIN -3.4028235E38
#define FMT "%f "
#endif

template<typename elem_t, typename acc_t>
class MlasSystolicMatmulTest : public MlasTestBase
{
private:
    MatrixGuardBuffer<elem_t> BufferA;
    MatrixGuardBuffer<elem_t> BufferB;
    MatrixGuardBuffer<elem_t> BufferC;
    MatrixGuardBuffer<elem_t> BufferCReference;
    MatrixGuardBuffer<acc_t> BufferBias;
    char acceleration_type_;

    inline elem_t saturate(acc_t num, float scale, bool relu) {
        num =  ACC_SCALE(num, scale);
        // Clip result
        num = num > ELEM_T_MAX ? ELEM_T_MAX : (num < ELEM_T_MIN ? ELEM_T_MIN : num);
        if (relu) {
            num = num < 0 ? 0 : num;
        }
        return num;
    }

    void mymatmul(int dimI, int dimJ, int dimK, const elem_t* in1, const elem_t* in2, elem_t* out,
                  float scale, bool relu, const acc_t* bias = nullptr) {
        for (int i = 0; i < dimI; i++) {
            for (int j = 0; j < dimJ; j++) {
                acc_t res = 0;
                for (int k = 0; k < dimK; k++) {
                    res += in1[i * dimK + k] * in2[k * dimJ + j];
                }
                out[i * dimJ + j] = saturate(res + (bias != nullptr ? bias[i * dimJ + j] : 0), scale, relu);
           }
        }
    }

    void NaiveCPUMultiply(int dimI, int dimJ, int dimK, const elem_t* in1, const elem_t* in2, elem_t* out,
                                 float scale, bool relu, const acc_t* bias = nullptr) {
        return mymatmul(dimI, dimJ, dimK, in1, in2, out, scale, relu, bias);
    }


    void Test(size_t M, size_t N, size_t K, float scale, int tolerance, bool relu = false) 
    {
        printf("Testing... %zu %zu %zu\n", M, N, K);
        const elem_t* A = BufferA.GetBuffer(K * M);
        const elem_t* B = BufferB.GetBuffer(N * K);
        const acc_t* Bias = BufferBias.GetBuffer(N * M);
        elem_t* C = BufferC.GetBuffer(N * M);
        elem_t* CReference = BufferCReference.GetBuffer(N * M);

        std::fill_n(C, M * N, -1);
        std::fill_n(CReference, M * N, -1);

        SystolicMultiply(acceleration_type_, relu, M, N, K, A, B, C, scale, Bias);
        NaiveCPUMultiply(M, N, K, A, B, CReference, scale, relu, Bias);

        for (size_t f = 0; f < M * N; f++) {
            if (ABS(C[f] - CReference[f]) > tolerance) {
                printf("A matrix:\n");
                for (size_t m = 0; m < M; m++) {
                    for (size_t k = 0; k < K; k++) {
                        printf(FMT, A[m * K + k]);
                    }
                    printf("\n");
                }
                printf("B matrix:\n");
                for (size_t k = 0; k < K; k++) {
                    for (size_t n = 0; n < N; n++) {
                        printf(FMT, B[k * N + n]);
                    }
                    printf("\n");
                }
                printf("Bias matrix:\n");
                for (size_t m = 0; m < M; m++) {
                    for (size_t n = 0; n < N; n++) {
                        printf(FMT, Bias[m * N + n]);
                    }
                    printf("\n");
                }
                printf("C matrix:\n");
                for (size_t m = 0; m < M; m++) {
                    for (size_t n = 0; n < N; n++) {
                        printf(FMT, C[m * N + n]);
                    }
                    printf("\n");
                }
                printf("C_ref matrix:\n");
                for (size_t m = 0; m < M; m++) {
                    for (size_t n = 0; n < N; n++) {
                        printf(FMT, CReference[m*N + n]);
                    }
                    printf("\n");
                }

                printf("mismatch M=%zd, N=%zd, K=%zd, scale=%f, relu=%d. Diff=" FMT "!\n", M, N, K, scale, relu, ABS(C[f] - CReference[f]) );
                return;
            }
        }
    }
    
public:
    void ExecuteShort(void) override
    {
        // Should match precisely for exact multiples of systolic size
        printf("Testing exact dimensions with no divisor\n");
        Test(16, 16, 16, 1, /*tolerance =*/  0);
        Test(1*16, 2*16, 3*16, 1, /*tolerance =*/  0);
        Test(16, 16, 16, 1, 0, /*relu= */ true);
        Test(1*16, 2*16, 3*16, 1, /*tolerance =*/  0, /*relu= */ true);

        // Should match preicsely for exact multiples with divisor (right shift)
        printf("Testing exact dimensions with divisor\n");
        Test(16, 16, 16, 0.25, /*tolerance =*/  0);
        Test(1*16, 2*16, 3*16, 2, /*tolerance =*/  0);
        Test(16, 16, 16, 0.0625, /*tolerance =*/ 0, /*relu= */ true);
        Test(1*16, 2*16, 3*16, 0.0625, /*tolerance =*/  0, /*relu= */ true);

        printf("Testing non-exact dimensions with divisor\n");
        Test(3, 5, 7, 0.25, /*tolerance= */0);
        Test(89, 79, 83, 0.0625, /*tolerance= */0);
        Test(18, 45, 337, 0.0039, /*tolerance= */ 0, /*relu= */ true);
        Test(1697, 2029, 1319, 0.00001525, /*tolerance =*/ 0, /*relu= */ true);
    }

    void ExecuteLong(void) override
    {
    }

    MlasSystolicMatmulTest(char acceleration_type) : acceleration_type_(acceleration_type) {}

};

#ifdef SYSTOLIC_INT8

template<typename elem_t, typename acc_t>
class MlasSystolicAddTest : public MlasTestBase
{
private:
    MatrixGuardBuffer<elem_t> BufferA;
    MatrixGuardBuffer<elem_t> BufferB;
    MatrixGuardBuffer<elem_t> BufferC;
    MatrixGuardBuffer<elem_t> BufferCReference;
    char acceleration_type_;

    inline elem_t saturate(acc_t num, float scale, bool relu) {
        num =  ACC_SCALE(num, scale);
        // Clip result
        num = num > ELEM_T_MAX ? ELEM_T_MAX : (num < ELEM_T_MIN ? ELEM_T_MIN : num);
        if (relu) {
            num = num < 0 ? 0 : num;
        }
        return num;
    }

    void NaiveCPUAdd(bool relu, const int8_t* in1, float in1_scale, const int8_t* in2, float in2_scale,
                int8_t* out, float out_scale, int dim) {
        for (int i = 0; i < dim; i++) {
            int32_t tmp1 = (int) MVIN_SCALE(*in1, in1_scale/out_scale);
            int32_t tmp2 = (int) MVIN_SCALE(*in2, in2_scale/out_scale);
            *out = saturate(tmp1 + tmp2, /*scale= */1, relu);

            out++;
            in1++;
            in2++;
        }
    }

    void Test(size_t M, float scaleA, float scaleB, float scaleOut, int tolerance, bool relu = false) 
    {
        printf("Testing... %zu\n", M);
        const elem_t* A = BufferA.GetBuffer(M);
        const elem_t* B = BufferB.GetBuffer(M);

        elem_t* C = BufferC.GetBuffer(M);
        elem_t* CReference = BufferCReference.GetBuffer(M);

        SystolicAdd(acceleration_type_, relu, A, scaleA, B, scaleB, C, scaleOut, M);
        NaiveCPUAdd(relu, A, scaleA, B, scaleB, CReference, scaleOut, M);

        for (size_t f = 0; f < M; f++) {
            if (ABS(C[f] - CReference[f]) > tolerance) {
                printf("mismatch M=%zd, scaleA=%f, scaleB=%f, scaleOut=%f relu=%d. Diff=" FMT "!\n", M, scaleA, scaleB, scaleOut, relu, ABS(C[f] - CReference[f]) );
                return;
            }
        }
    }
    
public:
    void ExecuteShort(void) override
    {
        Test(1, 0.01, 0.05, 0.01, /*tolerance =*/  0);
        Test(15, 0.01, 0.05, 0.01, /*tolerance =*/  0);
        Test(3*16, 0.01, 0.05, 0.01, /*tolerance =*/  0);
        Test(2*16, 0.5, 0.5, 0.25, /*tolerance =*/  0);
        Test(2*16 + 1, 0.5, 0.5, 0.25, /*tolerance =*/  0);
        Test(2*16 + 15, 0.3, 0.5, 0.25, /*tolerance =*/  0);
        Test(1697, 0.17, 0.01, 0.5, /*tolerance =*/  0, /*relu= */ true);
        Test(2029, 0.113, 0.01, 3, /*tolerance =*/  0, /*relu= */ true);
    }

    void ExecuteLong(void) override
    {
    }

    MlasSystolicAddTest(char acceleration_type) : acceleration_type_(acceleration_type) {}

};

#endif