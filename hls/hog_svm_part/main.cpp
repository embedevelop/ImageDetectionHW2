#include "/opt/Xilinx/Vivado/2018.2/include/gmp.h"
#include "hls_video.h"
#include <iostream>
#include <string.h>
#include <hls_stream.h>
#include <ap_axi_sdata.h>
#include <ap_fixed.h>
#include <iostream>
#include <iomanip>

//#include "consts.h"

#define BLOCK_NUM_H IMAGE_HEIGHT / CELL_SIZE - 1


//ypedef float ap_fixed_float;
//#define AP_FIXED_INT_WIDTH 10
typedef ap_fixed<32, 10> ap_fixed_float;
typedef ap_fixed<64, 20> accum_fixed;
//typedef ap_fixed<64,20,AP_RND> ap_fixed_float;
struct bgr{
	unsigned char b,g,r;
};


using namespace std;

#define IMAGE_WIDTH 320
#define IMAGE_HEIGHT 240
#define WINDOW_BLOCKNUM_W 7
#define WINDOW_BLOCKNUM_H 3
#define CELL_SIZE 8
#define BLOCK_SIZE 2
#define HIST_BIN_NUM 9
#define WINDOW_NUM_W IMAGE_WIDTH / CELL_SIZE - WINDOW_BLOCKNUM_W
#define BLOCK_NUM_W IMAGE_WIDTH / CELL_SIZE - 1
#define BLOCK_NUM_H IMAGE_HEIGHT / CELL_SIZE - 1

inline int approx_distance(int dx, int dy){
	int min, max; //uint
	if(dx < 0) dx = -dx;
	if(dy < 0) dy = -dy;

	if(dx < dy){
		min = dx;
		max = dy;
	}else{
		min = dy;
		max = dx;
	}

	//coefficients equivalent to (123/128 * max) and (51/128*min)
	return ((( max << 8 ) + ( max << 3 ) - ( max << 4 ) - ( max << 1 ) +
	            ( min << 7 ) - ( min << 5 ) + ( min << 3 ) - ( min << 1 )) >> 8 );
}

void grayscale_and_resizing(hls::stream<ap_axiu<32,1,1,1> >& bgr_in, hls::stream<ap_uint<8> >& gray_pix, hls::stream<bgr>& upper_scaled_rgb, hls::stream<bgr>& bottom_scaled_rgb){
	//scaleBuffer[i][j]
	//i indicates the kind of location in (8,8) cell
	//  0(1,3) 1(1,4) 2(2,3) 3(2,5) 4(5,3) 5(5,4) 6(6,3)
	//j indicates the cell x index
	bgr scaleBuffer[7][IMAGE_WIDTH/8];

#pragma HLS ARRAY_PARTITION variable=scaleBuffer complete dim=1
	for(int y = 0; y < IMAGE_HEIGHT / 8; y++){
		for(int yy = 0; yy < 8; yy++){
			for(int x = 0; x < IMAGE_WIDTH / 8; x++){
				for(int xx = 0; xx < 8; xx++){
#pragma HLS PIPELINE II=1
					bgr pix;
					ap_uint<32> indata = bgr_in.read().data;
					pix.b = (indata) & 255;
					pix.g = (indata >> 8) & 255;
					pix.r = (indata >> 16) & 255;

					//cout << (int)pix.b << " " << (int)pix.g << " " << (int)pix.r << endl;
					unsigned char gray = ((int)(299 * (int)pix.r + 587 * (int)pix.g + 114 * (int)pix.b) / 1000);
					gray_pix.write(gray);
					int yy2 = yy%4;
					if((yy2 == 1 || yy2 == 2) && (xx == 3 || xx == 4)){
						if(yy == 6 && xx == 4){
							//generate upper and bottom output
							bgr pix1, pix2, pix3, pix4, pix5, pix6, pix7, pix8;
							pix1 = scaleBuffer[0][x];
							pix2 = scaleBuffer[1][x];
							pix3 = scaleBuffer[2][x];
							pix4 = scaleBuffer[3][x];
							pix5 = scaleBuffer[4][x];
							pix6 = scaleBuffer[5][x];
							pix7 = scaleBuffer[6][x];
							pix8 = pix;
							int u_bsum = (int)pix1.b + (int)pix2.b + (int)pix3.b + (int)pix4.b;
							int u_gsum = (int)pix1.g + (int)pix2.g + (int)pix3.g + (int)pix4.g;
							int u_rsum = (int)pix1.r + (int)pix2.r + (int)pix3.r + (int)pix4.r;
							int b_bsum = (int)pix5.b + (int)pix6.b + (int)pix7.b + (int)pix8.b;
							int b_gsum = (int)pix5.g + (int)pix6.g + (int)pix7.g + (int)pix8.g;
							int b_rsum = (int)pix5.r + (int)pix6.r + (int)pix7.r + (int)pix8.r;
							bgr u_rst, b_rst;
							u_rst.b = u_bsum / 4 + (((u_bsum % 2) > 0) ? 1 : 0);//round
							u_rst.g = u_gsum / 4 + (((u_gsum % 2) > 0) ? 1 : 0);
							u_rst.r = u_rsum / 4 + (((u_rsum % 2) > 0) ? 1 : 0);
							b_rst.b = u_bsum / 4 + (((u_bsum % 2) > 0) ? 1 : 0);
							b_rst.g = u_gsum / 4 + (((u_gsum % 2) > 0) ? 1 : 0);
							b_rst.r = u_rsum / 4 + (((u_rsum % 2) > 0) ? 1 : 0);
							upper_scaled_rgb.write(u_rst);
							bottom_scaled_rgb.write(b_rst);
							//cout << y << " " << x << " " << u_rst.b << " " << b_rst.b << endl;
						}else{
							int bufindex = 4 * (yy / 4) + 2 * (yy2 / 2) + (xx - 3);
							//cout << bufindex << endl;
							scaleBuffer[bufindex][x] = pix;
						}
					}
				}
			}
		}
	}
}

void bgr2hsv(unsigned char bb, unsigned char gg, unsigned char rr, unsigned char* h, unsigned char* s, unsigned char* v){
	int b = (int)bb;
	int g = (int)gg;
	int r = (int)rr;
	int pix0_max = max(max(b, g), r);
	int pix0_min = min(min(b, g), r);
	int pix0_V = pix0_max;
	int pix0_S;
	int pix0_2H;
	if(pix0_max == pix0_min){
		pix0_2H = 0;
		pix0_S = 0;
	}else{
		if(pix0_max == r) pix0_2H = 60 * (g - b) / (pix0_V - pix0_min);
		else if(pix0_max == g) pix0_2H = 60 * (b - r) / (pix0_V - pix0_min) + 120;
		else pix0_2H = 60 * (r - g) / (pix0_V - pix0_min) + 240;

		if(pix0_2H > 360) pix0_2H = pix0_2H - 360;
		else if(pix0_2H  < 0) pix0_2H = pix0_2H + 360;

		pix0_S = (pix0_max - pix0_min) * 255 / pix0_max;
	}
	*h = ((unsigned char)(pix0_2H / 2));
	*s = (unsigned char)pix0_S;
	*v = (unsigned char)pix0_V;
}

struct pixweight{
	ap_uint<128> weight[3];
	//ap_fixed_float upper_hsvweight[3];
	//ap_fixed_float bottom_hsvweight[3];
	//ap_fixed_float upper_bgrweight[3];
	//ap_fixed_float bottom_bgrweight[3];
};
accum_fixed multiply_accum_bgr(ap_uint<128> weight, unsigned char uhsv, unsigned char bhsv, unsigned char ubgr, unsigned char bbgr){
/*#pragma HLS allocation instances=udiv limit=1 operation*/
	//As approximation, divide by 256 instead of 255.
	ap_uint<64> apuint_uhsv = uhsv;
	ap_uint<64> apuint_bhsv = bhsv;
	ap_uint<64> apuint_ubgr = ubgr;
	ap_uint<64> apuint_bbgr = bbgr;
	ap_fixed_float uhsv_fixed = 0;
	ap_fixed_float bhsv_fixed = 0;
	ap_fixed_float ubgr_fixed = 0;
	ap_fixed_float bbgr_fixed = 0;
	uhsv_fixed.range(21,14) = apuint_uhsv.range(7, 0);
	bhsv_fixed.range(21,14) = apuint_bhsv.range(7, 0);
	ubgr_fixed.range(21,14) = apuint_ubgr.range(7, 0);
	bbgr_fixed.range(21,14) = apuint_bbgr.range(7, 0);
	ap_fixed_float uhsv_weight = 0;
	ap_fixed_float bhsv_weight = 0;
	ap_fixed_float ubgr_weight = 0;
	ap_fixed_float bbgr_weight = 0;
	uhsv_weight.range(31, 0) = weight.range(127, 96);
	bhsv_weight.range(31, 0) = weight.range(95, 64);
	ubgr_weight.range(31, 0) = weight.range(63, 32);
	bbgr_weight.range(31, 0) = weight.range(31, 0);
#pragma HLS allocation instances=mul limit=2
	return (accum_fixed)uhsv_weight * (accum_fixed)uhsv_fixed + (accum_fixed)bhsv_weight * (accum_fixed)bhsv_fixed
			+ (accum_fixed)ubgr_weight * (accum_fixed)ubgr_fixed + (accum_fixed)bbgr_weight * (accum_fixed)bbgr_fixed;
}

//accum_fixed bgr_hsv_result[4661];

void bgr_hsv_svm_classification(hls::stream<bgr>& upper_scaled_in, hls::stream<bgr>& bottom_scaled_in, hls::stream<accum_fixed>& resultstream,
		pixweight w1[8], pixweight w2[8], pixweight w3[8], pixweight w4[8]){
	/*const pixweight WeightData[4][8] = {
	{{{0.020371287, 0.11754206, 0.052620558}, {0.048162602, 0.016815955, 0.0038571072}, {0.071730293, 0.065275003, 0.028889994}, {0.038901613, 0.0092229031, -0.028825374}},
	{{-0.089498951, 0.076741773, 0.075615231}, {0.16883909, 0.13240662, 0.019985014}, {0.059390586, 0.096840426, 0.092502714}, {0.029971676, 0.030376268, -0.026852187}},
	{{-0.018182268, -0.10030023, -0.11182791}, {-0.035702248, -0.10735345, -0.022564083}, {-0.10341782, -0.044062326, -0.052889815}, {0.013626721, -0.0042592695, -0.01899933}},
	{{-0.070917636, -0.070040604, -0.031416891}, {0.046079124, -0.11573138, -0.072439377}, {0.000101127, 0.0041335958, 0.041936163}, {-0.045337642, -0.081155537, -0.086017663}},
	{{0.31268987, -0.22161892, -0.11092621}, {0.092340299, -0.17459179, 0.050251555}, {-0.028027051, -0.028213295, -0.048870753}, {0.082514529, 0.052879155, 0.088748174}},
	{{0.26988158, 0.050662917, 0.013448453}, {-0.060604493, 0.036853602, 0.11274863}, {0.011320314, -0.019154544, 0.036684715}, {0.021456144, -0.0062324432, 0.17946469}},
	{{-0.026537339, -0.047144313, -0.02768047}, {-0.1237802, 0.27853941, 0.12615391}, {-0.066858783, -0.065760586, -0.020650061}, {-0.23890913, -0.23104086, 0.17132315}},
	{{-0.11053537, -0.032296846, -0.034950713}, {0.14434386, -0.005433003, 0.038525037}, {-0.055892395, -0.051928245, -0.040986292}, {-0.10243054, -0.13746209, 0.078968399}}}
	,
	{{{-0.10898625, 0.071634918, -0.029992231}, {-0.0053820727, 0.040743582, -0.042088092}, {-0.042829335, -0.040088393, -0.016294744}, {-0.076042711, -0.074801553, -0.015516881}},
	{{0.017519718, -0.27485937, -0.064405945}, {0.13548549, -0.35072745, -0.13058319}, {-0.057400295, -0.06095545, 0.081317958}, {-0.099203007, -0.11901927, 0.11115792}},
	{{-0.28023093, -0.16957001, 0.068558575}, {-0.23882319, -0.41990275, -0.041296008}, {-0.0063692765, 0.037837402, 0.14689748}, {-0.032404171, -0.042706346, 0.17808126}},
	{{0.1336384, -0.15564492, 0.024031315}, {-0.0046813673, -0.22746681, 0.12040746}, {0.019779467, -0.055303676, 0.047804219}, {0.12693606, -0.031140441, 0.19911166}},
	{{0.15402029, -0.059604621, -0.01972509}, {0.049276836, -0.088246575, -0.063674764}, {-0.1401801, -0.25512646, 0.063201893}, {-0.038890699, -0.36958643, -0.0022525514}},
	{{-0.14909845, -0.10986489, 0.088270913}, {-0.06782847, -0.090653813, 0.19508839}, {-0.020386143, -0.056992613, 0.20255332}, {0.028004038, -0.027865895, 0.31429582}},
	{{-0.21247009, 0.22126779, 0.22551715}, {-0.182488, 0.33588085, 0.35821497}, {-0.1392855, -0.16630134, 0.28910273}, {-0.034709853, -0.0090028481, 0.43609151}},
	{{-0.055420982, 0.078221327, 0.05364981}, {0.089923835, 0.13620772, 0.067854636}, {-0.08955006, -0.12499049, 0.060785142}, {-0.14470767, -0.13994718, 0.11232396}}}
	,
	{{{0.0013239678, 0.055428806, -0.082635795}, {-0.0022864108, 0.11964768, 0.089203251}, {-0.077817534, -0.10342703, -0.073800935}, {0.10595673, 0.03043694, 0.067686307}},
	{{-0.14079345, -0.20681809, -0.081689524}, {0.073497492, -0.13241872, -0.1133071}, {-0.078411984, -0.093166841, 0.085428721}, {-0.077906801, -0.072230316, -0.048655656}},
	{{0.046048192, -0.23297896, -0.18809364}, {-0.092131011, -0.14063722, -0.10003229}, {-0.14031869, -0.23507535, -0.026911787}, {-0.071456403, -0.14701655, -0.054165025}},
	{{0.16404433, -0.17801143, 0.069624105}, {0.19188991, -0.037026479, -0.041774378}, {0.14016975, -0.056543752, 0.085056077}, {-0.0099750322, -0.22112571, -0.074386097}},
	{{0.077273519, -0.084951963, 0.098354623}, {0.071026204, 0.07886672, 0.060001722}, {0.023768544, -0.35530938, 0.12471573}, {-0.22384476, -0.51996968, 0.061906871}},
	{{0.060351168, -0.14079133, 0.18709987}, {-0.048486703, -0.02170411, 0.081485848}, {0.10663695, -0.022133559, 0.25735073}, {-0.1034866, -0.21921642, 0.11522512}},
	{{-0.099991538, 0.28472686, 0.32885018}, {0.15917123, 0.016225102, 0.22883522}, {0.029644417, 0.039079195, 0.39522995}, {0.071990856, 0.0034405584, 0.26425154}},
	{{-0.13240705, 0.10894868, 0.075737005}, {0.19224053, -0.086145875, -0.0094230694}, {-0.11340512, -0.17253377, 0.12304173}, {-0.049533709, -0.11990684, 0.025364579}}}
	,
	{{{0.014678718, 0.14280875, -0.033435076}, {0.16255002, -0.021581039, 0.097843459}, {-0.023842296, -0.081018992, -0.04608475}, {0.12125819, 0.083366695, 0.05939952}},
	{{-0.016093116, -0.10314063, -0.0056518201}, {0.24508968, -0.080563156, 0.052207287}, {0.073650219, -0.0085824543, -0.037905219}, {0.11461151, 0.060213669, -0.0056508531}},
	{{0.027626555, -0.090248633, 0.006389358}, {0.10143696, 0.015023398, 0.050473112}, {0.098815972, -0.0029400005, -0.038300807}, {0.12029697, 0.085480937, -0.037544015}},
	{{0.011329069, -0.0006998653, 0.095261521}, {0.11940634, 0.071801385, 0.0077632778}, {0.12782293, 0.054844184, -0.018921885}, {0.040403021, 0.00074267527, -0.063218363}},
	{{0.0316921, 0.096954359, 0.21035329}, {0.35093432, -0.045245094, 0.056426753}, {0.079929289, -0.034457723, 0.096275703}, {0.021939321, -0.096427054, -0.020224142}},
	{{0.19797268, 0.062644908, 0.089984598}, {0.30734046, 0.0099592396, 0.072560425}, {-0.14946462, -0.25150766, 0.065191361}, {-0.071131307, -0.19794033, 0.039840833}},
	{{0.043288449, 0.090454614, 0.13972389}, {0.11814476, 0.064859977, 0.068014676}, {-0.012816126, -0.09050149, 0.11790795}, {-0.033003706, -0.13534811, -0.0011606731}},
	{{0.28397242, -0.040270352, -0.021484664}, {0.33941849, -0.14723122, -0.035179396}, {-0.035080773, -0.14034925, -0.036054036}, {0.022488399, -0.013106878, -0.072457735}}}

	};*/
	accum_fixed PartialSum[4][(IMAGE_WIDTH / 8) - 8 + 1];
//#pragma HLS RESOURCE variable=WeightData core=ROM_1P_BRAM
//#pragma HLS ARRAY_PARTITION variable=WeightData complete dim=1
#pragma HLS ARRAY_PARTITION variable=PartialSum complete dim=1
#pragma HLS RESOURCE variable=PartialSum[0] core=RAM_2P_BRAM
#pragma HLS RESOURCE variable=PartialSum[1] core=RAM_2P_BRAM
#pragma HLS RESOURCE variable=PartialSum[2] core=RAM_2P_BRAM
#pragma HLS RESOURCE variable=PartialSum[3] core=RAM_2P_BRAM
	for(int i = 0; i < 4; i++){
		for(int j = 0; j < ((IMAGE_WIDTH / 8) - 8 + 1); j++){
#pragma HLS PIPELINE II = 1
			PartialSum[i][j] = 0;
		}
	}
	int rstcnt = 0;
	for(int y = 0; y < IMAGE_HEIGHT / 8; y++){
		for(int x = 0; x < IMAGE_WIDTH / 8; x++){
//#pragma HLS PIPELINE II=1
			bgr upper_bgr = upper_scaled_in.read();
			bgr bottom_bgr = bottom_scaled_in.read();
			//h->b s->g v->r
			bgr upper_hsv, bottom_hsv;
			bgr2hsv(upper_bgr.b, upper_bgr.g, upper_bgr.r, &upper_hsv.b, &upper_hsv.g, &upper_hsv.r);
			bgr2hsv(bottom_bgr.b, bottom_bgr.g, bottom_bgr.r, &bottom_hsv.b, &bottom_hsv.g, &bottom_hsv.r);

			for(int cell_index_x = 7; cell_index_x >= 0; cell_index_x--){
#pragma HLS PIPELINE II=1
				int winx = x - cell_index_x;
				bool inside_window = (cell_index_x <= x && x <= cell_index_x + (IMAGE_WIDTH / 8 - 8));
				if(inside_window){
					for(int cell_index_y = 0; cell_index_y < 4; cell_index_y++){
						int cell_start_y = y - cell_index_y;
						if(0 <= cell_start_y && cell_start_y <= (IMAGE_HEIGHT / 8 - 4)){
							int partial_sum_index_y = (y - cell_index_y) % 4;
							//pixweight w = WeightData[cell_index_y][cell_index_x];
							pixweight w;
							if(cell_index_y == 0) w = w1[cell_index_x];
							else if(cell_index_y == 1) w = w2[cell_index_x];
							else if(cell_index_y == 2) w = w3[cell_index_x];
							else w = w4[cell_index_x];
#pragma HLS allocation instances=multiply_accum_bgr limit=1 function
//#pragma HLS DATAFLOW
							accum_fixed tmp_partial_sum = multiply_accum_bgr(w.weight[0], upper_hsv.b, bottom_hsv.b, upper_bgr.b, bottom_bgr.b)
									+multiply_accum_bgr(w.weight[1], upper_hsv.g, bottom_hsv.g, upper_bgr.g, bottom_bgr.g)
									+multiply_accum_bgr(w.weight[2], upper_hsv.r, bottom_hsv.r, upper_bgr.r, bottom_bgr.r);
							if(cell_index_x == 0 && cell_index_y == 0) PartialSum[partial_sum_index_y][winx] = tmp_partial_sum;
							else PartialSum[partial_sum_index_y][winx] += tmp_partial_sum;
							bool window_completed = (cell_index_x == 7 && cell_index_y == 3);
							if(window_completed){
								accum_fixed allsum = PartialSum[partial_sum_index_y][winx];
								//bgr_hsv_result[rstcnt++] = allsum;
								resultstream.write(allsum);
								//PartialSum[partial_sum_index_y][winx] = 0;
							}
						}
					}
				}
			}
		}
	}
}
//mag minimum:0 maximum:sqrt(255*255+255*255) < 2^9
//integer bit needs 9 + 1(sign) = 10bit
//typedef ap_fixed<18,10,AP_RND> magnitude_fixed;
//typedef float magnitude_fixed;
typedef int magnitude_fixed;

void compute_mag_and_bin(hls::stream<ap_uint<8> >& instream, hls::stream<int>& magstream, hls::stream<int>& binstream){
	//Lookup tables for tan(20)*Gx
	int lut0[256] = {0,0,0,1,1,1,2,2,2,3,3,3,4,4,5,5,5,6,6,6,7,7,7,8,8,9,9,9,10,10,10,11,11,11,12,12,13,13,13,14,14,14,15,15,15,16,16,17,17,17,18,18,18,19,19,19,20,20,21,21,21,22,22,22,23,23,23,24,24,25,25,25,26,26,26,27,27,27,28,28,29,29,29,30,30,30,31,31,31,32,32,33,33,33,34,34,34,35,35,35,36,36,37,37,37,38,38,38,39,39,39,40,40,41,41,41,42,42,42,43,43,43,44,44,45,45,45,46,46,46,47,47,47,48,48,49,49,49,50,50,50,51,51,51,52,52,53,53,53,54,54,54,55,55,55,56,56,57,57,57,58,58,58,59,59,59,60,60,61,61,61,62,62,62,63,63,63,64,64,65,65,65,66,66,66,67,67,67,68,68,69,69,69,70,70,70,71,71,71,72,72,73,73,73,74,74,74,75,75,75,76,76,77,77,77,78,78,78,79,79,79,80,80,81,81,81,82,82,82,83,83,83,84,84,85,85,85,86,86,86,87,87,87,88,88,89,89,89,90,90,90,91,91,91,92,92};
#pragma HLS RESOURCE variable=lut0 core=ROM_1P_BRAM
	//Lookup tables for tan(40)*Gx
	int lut1[256] = {0,0,1,2,3,4,5,5,6,7,8,9,10,10,11,12,13,14,15,15,16,17,18,19,20,20,21,22,23,24,25,26,26,27,28,29,30,31,31,32,33,34,35,36,36,37,38,39,40,41,41,42,43,44,45,46,46,47,48,49,50,51,52,52,53,54,55,56,57,57,58,59,60,61,62,62,63,64,65,66,67,67,68,69,70,71,72,72,73,74,75,76,77,78,78,79,80,81,82,83,83,84,85,86,87,88,88,89,90,91,92,93,93,94,95,96,97,98,98,99,100,101,102,103,104,104,105,106,107,108,109,109,110,111,112,113,114,114,115,116,117,118,119,119,120,121,122,123,124,124,125,126,127,128,129,130,130,131,132,133,134,135,135,136,137,138,139,140,140,141,142,143,144,145,145,146,147,148,149,150,150,151,152,153,154,155,156,156,157,158,159,160,161,161,162,163,164,165,166,166,167,168,169,170,171,171,172,173,174,175,176,177,177,178,179,180,181,182,182,183,184,185,186,187,187,188,189,190,191,192,192,193,194,195,196,197,197,198,199,200,201,202,203,203,204,205,206,207,208,208,209,210,211,212,213,213};
#pragma HLS RESOURCE variable=lut1 core=ROM_1P_BRAM
	//Lookup tables for tan(60)*Gx
	int lut2[256] = {0,1,3,5,6,8,10,12,13,15,17,19,20,22,24,25,27,29,31,32,34,36,38,39,41,43,44,46,48,50,51,53,55,57,58,60,62,63,65,67,69,70,72,74,76,77,79,81,83,84,86,88,89,91,93,95,96,98,100,102,103,105,107,108,110,112,114,115,117,119,121,122,124,126,127,129,131,133,134,136,138,140,141,143,145,147,148,150,152,153,155,157,159,160,162,164,166,167,169,171,172,174,176,178,179,181,183,185,186,188,190,191,193,195,197,198,200,202,204,205,207,209,210,212,214,216,217,219,221,223,224,226,228,230,231,233,235,236,238,240,242,243,245,247,249,250,252,254,255,257,259,261,262,264,266,268,269,271,273,274,276,278,280,281,283,285,287,288,290,292,294,295,297,299,300,302,304,306,307,309,311,313,314,316,318,319,321,323,325,326,328,330,332,333,335,337,338,340,342,344,345,347,349,351,352,354,356,358,359,361,363,364,366,368,370,371,373,375,377,378,380,382,383,385,387,389,390,392,394,396,397,399,401,402,404,406,408,409,411,413,415,416,418,420,421,423,425,427,428,430,432,434,435,437,439,441};
#pragma HLS RESOURCE variable=lut2 core=ROM_1P_BRAM
	//Lookup tables for tan(80)*Gx
	int lut3[256] = {0,5,11,17,22,28,34,39,45,51,56,62,68,73,79,85,90,96,102,107,113,119,124,130,136,141,147,153,158,164,170,175,181,187,192,198,204,209,215,221,226,232,238,243,249,255,260,266,272,277,283,289,294,300,306,311,317,323,328,334,340,345,351,357,362,368,374,379,385,391,396,402,408,413,419,425,430,436,442,447,453,459,464,470,476,481,487,493,498,504,510,515,521,527,532,538,544,549,555,561,566,572,578,584,589,595,601,606,612,618,623,629,635,640,646,652,657,663,669,674,680,686,691,697,703,708,714,720,725,731,737,742,748,754,759,765,771,776,782,788,793,799,805,810,816,822,827,833,839,844,850,856,861,867,873,878,884,890,895,901,907,912,918,924,929,935,941,946,952,958,963,969,975,980,986,992,997,1003,1009,1014,1020,1026,1031,1037,1043,1048,1054,1060,1065,1071,1077,1082,1088,1094,1099,1105,1111,1116,1122,1128,1133,1139,1145,1150,1156,1162,1168,1173,1179,1185,1190,1196,1202,1207,1213,1219,1224,1230,1236,1241,1247,1253,1258,1264,1270,1275,1281,1287,1292,1298,1304,1309,1315,1321,1326,1332,1338,1343,1349,1355,1360,1366,1372,1377,1383,1389,1394,1400,1406,1411,1417,1423,1428,1434,1440,1445};
#pragma HLS RESOURCE variable=lut3 core=ROM_1P_BRAM

	hls::LineBuffer<3, IMAGE_WIDTH, unsigned char> linebuf;
	hls::Window<3, 3, unsigned char> winbuf;

	//calculate magnitude using shift register
	loop_y:for(int y = 0; y < IMAGE_HEIGHT; y++){
		loop_x:for(int x = 0; x < IMAGE_WIDTH; x++){
#pragma HLS PIPELINE II=1
			bool isedge = (x < 2 || y < 2);

			unsigned char indata = instream.read();
			//these operation will be executed in parallel.
			linebuf.shift_pixels_up(x);
			linebuf.insert_bottom_row(indata, x);
			winbuf.shift_pixels_right();
			winbuf.insert_pixel(linebuf.getval(0, x), 0, 0);
			winbuf.insert_pixel(linebuf.getval(1, x), 1, 0);
			winbuf.insert_pixel(linebuf.getval(2, x), 2, 0);

			int Gx = isedge ? 0 : (int)winbuf.getval(1, 0) - (int)winbuf.getval(1, 2);
			int Gy = isedge ? 0 : (int)winbuf.getval(2, 1) - (int)winbuf.getval(0, 1);
			int square_sum = Gx * Gx + Gy * Gy;
			magnitude_fixed mag = (isedge||square_sum==0) ? 0 : approx_distance(Gx, Gy);//hls::sqrt(square_sum);
			int bin_index;
			if((Gx >= 0 && Gy >= 0) || (Gx <= 0 && Gy <= 0)){
				if 		(abs(Gy) < lut0[abs(Gx)]) 	{ bin_index = 0;
				}else if (abs(Gy) < lut1[abs(Gx)]) 	{ bin_index = 1;
				}else if (abs(Gy) < lut2[abs(Gx)]) 	{ bin_index = 2;
				}else if (abs(Gy) < lut3[abs(Gx)]) 	{ bin_index = 3;
				}else{ 								  bin_index = 4;
				}
			} else{
				if 		(abs(Gy) <  lut0[abs(Gx)])	{ bin_index = 4;
				}else if (abs(Gy) <  lut1[abs(Gx)]) { bin_index = 5;
				}else if (abs(Gy) <  lut2[abs(Gx)]) { bin_index = 6;
				}else if (abs(Gy) <  lut3[abs(Gx)]) { bin_index = 7;
				}else 								{ bin_index = 8;
				}
			}
			magstream.write(mag);
			binstream.write(bin_index);
		}
	}
}

struct ap_fixed9_float{
	ap_fixed_float data[9];
};

//bottom,upper minimum:0 maximum:sqrt(255*255+255*255)*8*8 < 2^15
//integer bit needs 15 + 1(sign) = 16bit
//typedef ap_fixed<24,16,AP_RND> blockpart_fixed;
//typedef float blockpart_fixed;
typedef int blockpart_fixed;

//8pix * 8pix = cellsize part of block
struct blockpart_fixed_9{
	blockpart_fixed data[HIST_BIN_NUM];
};

void cell_histogram_generate(hls::stream<magnitude_fixed>& magstream, hls::stream<int>& binstream,
		hls::stream<blockpart_fixed_9>& bottom, hls::stream<blockpart_fixed_9>& upper){

	hls::LineBuffer<CELL_SIZE, IMAGE_WIDTH/CELL_SIZE, blockpart_fixed > linebufs[HIST_BIN_NUM];
	hls::LineBuffer<2, IMAGE_WIDTH/CELL_SIZE, blockpart_fixed > cellbuf[HIST_BIN_NUM];
#pragma HLS ARRAY_PARTITION variable=linebufs complete dim=1
#pragma HLS ARRAY PARTITION variable=cellbuf complete dim=1
	blockpart_fixed vote_counter[HIST_BIN_NUM];
	for(int i = 0; i < HIST_BIN_NUM; i++) vote_counter[i] = 0;
#pragma HLS ARRAY_PARTITION variable=vote_counter complete dim=1
	loop_y:for(int y = 0; y < IMAGE_HEIGHT; y++){//480
		loop_winx:for(int winx = 0; winx < IMAGE_WIDTH / CELL_SIZE; winx++){ //80
			loop_cell_index:for(int cell_index = 0; cell_index < CELL_SIZE; cell_index++){ //8
#pragma HLS PIPELINE II=1
				magnitude_fixed mag = magstream.read();
				int bin = binstream.read();
				vote_counter[bin] += mag;
				if(cell_index == (CELL_SIZE - 1)){
					loop_updatelinebuf:for(int i = 0; i < HIST_BIN_NUM; i++){
						linebufs[i].shift_pixels_up(winx);
						linebufs[i].insert_bottom_row(vote_counter[i], winx);
					}
					if(y%CELL_SIZE == (CELL_SIZE - 1)){
						blockpart_fixed_9 out_upper, out_bottom;
						loop_cellbuf_calc:for(int bin_index = 0; bin_index < HIST_BIN_NUM; bin_index++){
							blockpart_fixed sum_of_cell = linebufs[bin_index].getval(0, winx) + linebufs[bin_index].getval(1, winx) + linebufs[bin_index].getval(2, winx) + linebufs[bin_index].getval(3, winx) + linebufs[bin_index].getval(4, winx) + linebufs[bin_index].getval(5, winx) + linebufs[bin_index].getval(6, winx) + linebufs[bin_index].getval(7, winx);
							cellbuf[bin_index].shift_pixels_up(winx);
							cellbuf[bin_index].insert_bottom_row(sum_of_cell, winx);
							if(y >= CELL_SIZE * BLOCK_SIZE - 1){
								out_upper.data[bin_index] = cellbuf[bin_index].getval(0, winx);
								out_bottom.data[bin_index] = cellbuf[bin_index].getval(1, winx);
								if(bin_index == 8){
									bottom << out_bottom;
									upper << out_upper;
								}
							}
						}
					}
					//zeroing
					for(int i = 0; i < HIST_BIN_NUM; i++) vote_counter[i] = 0;
				}
			}
		}
	}
}


//bottom,upper minimum:0 maximum:sqrt(255*255+255*255)*16*16 < 2^17
//integer bit needs 17 + 1(sign) = 18bit
//typedef ap_fixed<24,18,AP_RND> blocksum_fixed;
//typedef float blocksum_fixed;
typedef int blocksum_fixed;

inline blocksum_fixed myabs(blocksum_fixed input){
	return ((input > 0) ? (blocksum_fixed)input : (blocksum_fixed)(-input));
}

ap_fixed_float div_int_to_ap_fixed(blockpart_fixed a, blocksum_fixed b){
	if(a == 0 || b == 0) return (ap_fixed_float)0;
	ap_uint<64> aa = a;
	ap_uint<64> bb = b;
	ap_uint<64> target_a = aa << 32;
	ap_uint<64> target_b = bb << 16;
	ap_uint<64> c = target_a / target_b;

	ap_fixed_float ans = 0;
	ans.range(22,6) = c.range(16,0);
	return ans;
}
void block_histogram_normalization(hls::stream<blockpart_fixed_9>& bottom, hls::stream<blockpart_fixed_9>& upper,
		hls::stream<ap_fixed9_float>& ul_out, hls::stream<ap_fixed9_float>& ur_out, hls::stream<ap_fixed9_float>& bl_out, hls::stream<ap_fixed9_float>& br_out){
	hls::LineBuffer<2, 1, blockpart_fixed> bottomfifo[HIST_BIN_NUM], upperfifo[HIST_BIN_NUM];
	blocksum_fixed  partial_old_block_sum = 0;
#pragma HLS ARRAY_PARTITION variable=bottomfifo complete dim=1
#pragma HLS ARRAY PARTITION variable=upperfifo complete dim=1
	for(int y = 0; y < (IMAGE_HEIGHT / CELL_SIZE - BLOCK_SIZE + 1); y++){//59
		for(int x = 0; x < (IMAGE_WIDTH / CELL_SIZE); x++){//80
			blockpart_fixed_9 bottom_in = bottom.read();
			blockpart_fixed_9 upper_in = upper.read();
			ap_fixed9_float ul, ur, bl, br;

			blocksum_fixed partial_block_new_sum = 0;

			for(int bin_index = 0; bin_index < HIST_BIN_NUM; bin_index++){
#pragma HLS PIPELINE II=1
				blockpart_fixed b = bottom_in.data[bin_index];
				blockpart_fixed u = upper_in.data[bin_index];
				bottomfifo[bin_index].shift_pixels_up(0);
				bottomfifo[bin_index].insert_bottom_row(b, 0);
				upperfifo[bin_index].shift_pixels_up(0);
				upperfifo[bin_index].insert_bottom_row(u, 0);

				partial_block_new_sum += (b + u);
			}
			bool sum_of_block_completed = (x >= 1);
			if(sum_of_block_completed){
				blocksum_fixed block_sum = partial_block_new_sum + partial_old_block_sum;
				for(int bin_index = 0; bin_index < HIST_BIN_NUM; bin_index++){
	#pragma HLS PIPELINE II=1
					//59*79
					//normalization
					blockpart_fixed un_upperleft = upperfifo[bin_index].getval(0, 0);
					blockpart_fixed un_upperright = upperfifo[bin_index].getval(1, 0);
					blockpart_fixed un_bottomleft = bottomfifo[bin_index].getval(0, 0);
					blockpart_fixed un_bottomright = bottomfifo[bin_index].getval(1, 0);
#pragma HLS allocation instances=div_int_to_ap_fixed limit=1 function
					ap_fixed_float upperleft = div_int_to_ap_fixed(un_upperleft, block_sum);
					ap_fixed_float upperright = div_int_to_ap_fixed(un_upperright, block_sum);
					ap_fixed_float bottomleft = div_int_to_ap_fixed(un_bottomleft, block_sum);
					ap_fixed_float bottomright = div_int_to_ap_fixed(un_bottomright, block_sum);
					//cout << (int)un_upperleft << " " << (int)block_sum << " " << fixed << setprecision(10) << upperleft << endl;
					ul.data[bin_index] = upperleft;
					ur.data[bin_index] = upperright;
					bl.data[bin_index] = bottomleft;
					br.data[bin_index] = bottomright;
				}
				ul_out << ul;
				ur_out << ur;
				bl_out << bl;
				br_out << br;

			}
			partial_old_block_sum = partial_block_new_sum;
		}
	}
}

/*struct histdata{
	ap_fixed_float data[9];
};*/
struct hogweight{
	//histdata upperleft;
	//histdata upperright;
	//histdata bottomleft;
	//histdata bottomright;
	ap_uint<128> weightval[9];
};


accum_fixed multiply_accum_hog(ap_uint<128> weight, ap_fixed_float ul, ap_fixed_float ur, ap_fixed_float bl, ap_fixed_float br){
	ap_fixed_float ul_weight = 0;
	ap_fixed_float ur_weight = 0;
	ap_fixed_float bl_weight = 0;
	ap_fixed_float br_weight = 0;
	ul_weight.range(31, 0) = weight.range(127, 96);
	ur_weight.range(31, 0) = weight.range(95, 64);
	bl_weight.range(31, 0) = weight.range(63, 32);
	br_weight.range(31, 0) = weight.range(31, 0);
	#pragma HLS allocation instances=mul limit=2
	return (accum_fixed)ul_weight * (accum_fixed)ul + (accum_fixed)ur_weight * (accum_fixed)ur + (accum_fixed)bl_weight * (accum_fixed)bl + (accum_fixed)br_weight * (accum_fixed)br;
}
void hog_svm_classification(hls::stream<ap_fixed9_float>& upperleft, hls::stream<ap_fixed9_float>& upperright, hls::stream<ap_fixed9_float>& bottomleft, hls::stream<ap_fixed9_float>& bottomright,
		hls::stream<accum_fixed>& resultstream, hogweight w1[7], hogweight w2[7], hogweight w3[7]){

	/*weight WeightData[WINDOW_BLOCKNUM_H][WINDOW_BLOCKNUM_W] = {
	{{{-0.020430088043212890625, -0.017955780029296875, -0.0637295246124267578125, -0.018318653106689453125, 0.0344316959381103515625, -0.09418582916259765625, -0.2703206539154052734375, -0.1507284641265869140625, 0.209075450897216796875},{-0.021728992462158203125, 0.166233062744140625, -0.009647369384765625, 0.1395256519317626953125, 0.0637514591217041015625, 0.0468852519989013671875, -0.07170200347900390625, -0.087221622467041015625, 0.0221436023712158203125},{0.015216350555419921875, 0.066412448883056640625, -0.0578062534332275390625, 0.035459995269775390625, -0.08220767974853515625, 0.061465740203857421875, 0.0196068286895751953125, -0.074980258941650390625, 0.2188866138458251953125},{-0.044877529144287109375, -0.0183203220367431640625, 0.0238387584686279296875, -0.0877244472503662109375, -0.005450725555419921875, 0.1781313419342041015625, 0.0186116695404052734375, -0.074532985687255859375, 0.086177349090576171875}},
	{{-0.012218952178955078125, 0.1627256870269775390625, -0.0175497531890869140625, 0.155663013458251953125, 0.135300159454345703125, 0.1663110256195068359375, -0.008053302764892578125, -0.09210872650146484375, 0.0627148151397705078125},{-0.2366716861724853515625, -0.259692668914794921875, 0.00426578521728515625, -0.0479276180267333984375, -0.066269397735595703125, -0.11805057525634765625, 0.192030429840087890625, -0.0374953746795654296875, 0.1761262416839599609375},{-0.051277637481689453125, -0.0122025012969970703125, 0.078645229339599609375, -0.1183888912200927734375, -0.057731151580810546875, -0.1308944225311279296875, -0.1038444042205810546875, -0.0746822357177734375, 0.0205447673797607421875},{0.00824451446533203125, 0.021021366119384765625, -0.04734039306640625, 0.0500049591064453125, 0.029080867767333984375, 0.111240863800048828125, 0.2182939052581787109375, 0.00374698638916015625, 0.0012719631195068359375}},
	{{-0.157608509063720703125, -0.31154346466064453125, 0.045083522796630859375, 0.0863420963287353515625, -0.05441379547119140625, -0.1110031604766845703125, 0.270878314971923828125, -0.009723186492919921875, 0.3116266727447509765625},{-0.06541728973388671875, 0.20284271240234375, 0.164892673492431640625, 0.0336210727691650390625, -0.1107451915740966796875, 0.071824550628662109375, 0.171666622161865234375, -0.0717594623565673828125, -0.0590531826019287109375},{-0.023344516754150390625, 0.0589826107025146484375, -0.0721988677978515625, 0.0384504795074462890625, -0.0697124004364013671875, -0.01961517333984375, -0.0338089466094970703125, -0.070092678070068359375, -0.044692516326904296875},{-0.0115578174591064453125, 0.0185925960540771484375, 0.0554010868072509765625, -0.109764575958251953125, -0.0007760524749755859375, 0.1224462985992431640625, 0.061420440673828125, 0.070434093475341796875, 0.075275897979736328125}},
	{{-0.0992434024810791015625, 0.1143677234649658203125, 0.2904708385467529296875, 0.02082061767578125, -0.057289600372314453125, 0.0620324611663818359375, 0.2103817462921142578125, 0.0074326992034912109375, 0.1518154144287109375},{-0.266686916351318359375, -0.0682599544525146484375, 0.104794979095458984375, -0.0179564952850341796875, -0.015001773834228515625, -0.0369021892547607421875, 0.0283756256103515625, -0.1242122650146484375, 0.0462372303009033203125},{-0.0572655200958251953125, 0.1740696430206298828125, 0.0133535861968994140625, 0.000331401824951171875, -0.0514504909515380859375, -0.185492992401123046875, -0.1465313434600830078125, -0.02286052703857421875, 0.04119205474853515625},{-0.00014781951904296875, -0.035805225372314453125, -0.013472080230712890625, 0.0171253681182861328125, -0.05197048187255859375, 0.06090545654296875, 0.0144555568695068359375, -0.011081695556640625, 0.08130359649658203125}},
	{{-0.065496921539306640625, -0.0609023571014404296875, 0.0594861507415771484375, -0.12030792236328125, 0.0065708160400390625, -0.0992412567138671875, -0.0095527172088623046875, -0.094918727874755859375, 0.15382480621337890625},{-0.1285431385040283203125, -0.105930805206298828125, -0.193308353424072265625, 0.0154476165771484375, -0.01101398468017578125, -0.003024578094482421875, 0.236657619476318359375, -0.0523068904876708984375, -0.1196372509002685546875},{0.15976619720458984375, 0.1452453136444091796875, 0.13344669342041015625, 0.1726024150848388671875, -0.0667335987091064453125, 0.0086891651153564453125, -0.0375497341156005859375, 0.0149505138397216796875, 0.0573215484619140625},{-0.05297756195068359375, -0.08045673370361328125, 0.08265399932861328125, -0.130456447601318359375, 0.02265262603759765625, 0.1006069183349609375, 0.1705722808837890625, 0.0043151378631591796875, -0.1279704570770263671875}},
	{{-0.088866710662841796875, -0.0288369655609130859375, -0.1505768299102783203125, 0.00316143035888671875, -0.049936771392822265625, -0.0520236492156982421875, 0.1163604259490966796875, -0.1622126102447509765625, -0.138072967529296875},{-0.1837503910064697265625, -0.179034709930419921875, -0.0739858150482177734375, -0.040098667144775390625, -0.1136701107025146484375, -0.13188648223876953125, -0.1732332706451416015625, -0.12080860137939453125, 0.116583347320556640625},{0.0960276126861572265625, 0.1147186756134033203125, 0.1928489208221435546875, 0.0473651885986328125, 0.084230899810791015625, -0.012053012847900390625, -0.0703871250152587890625, 0.0606367588043212890625, -0.0161778926849365234375},{0.0083878040313720703125, 0.1217439174652099609375, 0.1376245021820068359375, -0.03405857086181640625, 0.012327671051025390625, 0.2181508541107177734375, 0.1524326801300048828125, -0.0238435268402099609375, 0.025859832763671875}},
	{{-0.22383785247802734375, -0.0698626041412353515625, -0.04840373992919921875, -0.0437886714935302734375, -0.0872952938079833984375, -0.307769298553466796875, -0.117016315460205078125, -0.1388208866119384765625, 0.1077158451080322265625},{-0.39144802093505859375, -0.3657543659210205078125, -0.0808832645416259765625, -0.250185489654541015625, -0.0124762058258056640625, -0.21907329559326171875, -0.079659938812255859375, -0.07649326324462890625, 0.278480052947998046875},{0.0424830913543701171875, 0.1299998760223388671875, 0.2907626628875732421875, 0.08179378509521484375, 0.00763034820556640625, 0.0566542148590087890625, 0.0774059295654296875, -0.0307104587554931640625, 0.003047466278076171875},{0.1270854473114013671875, -0.33081340789794921875, -0.257782459259033203125, -0.2101686000823974609375, 0.045318126678466796875, 0.29022979736328125, 0.386081218719482421875, 0.117023468017578125, -0.0400259494781494140625}}},
	{{{0.0282337665557861328125, 0.1129515171051025390625, -0.0437629222869873046875, 0.124366283416748046875, -0.0935680866241455078125, 0.1657154560089111328125, 0.0464947223663330078125, -0.0134642124176025390625, 0.252895355224609375},{-0.08544635772705078125, 0.024125576019287109375, 0.1302630901336669921875, -0.0819737911224365234375, -0.0547459125518798828125, -0.0854561328887939453125, -0.131814479827880859375, -0.078709125518798828125, -0.0224349498748779296875},{-0.0659236907958984375, -0.0491502285003662109375, -0.0402200222015380859375, -0.1021206378936767578125, 0.043684482574462890625, 0.0392529964447021484375, 0.1042716503143310546875, 0.057909488677978515625, 0.16062450408935546875},{0.043014049530029296875, 0.10195636749267578125, 0.019969463348388671875, -0.02417755126953125, 0.02384185791015625, 0.1018865108489990234375, 0.0988309383392333984375, -0.063949108123779296875, -0.009643077850341796875}},
	{{-0.077638149261474609375, -0.0372736454010009765625, 0.0108940601348876953125, -0.1485083103179931640625, -0.0573203563690185546875, -0.0239822864532470703125, -0.0578987598419189453125, -0.0479295253753662109375, 0.00946807861328125},{0.05960559844970703125, 0.0086205005645751953125, -0.0805690288543701171875, -0.0342557430267333984375, 0.0266792774200439453125, 0.0680267810821533203125, 0.171844959259033203125, -0.06665134429931640625, -0.0535666942596435546875},{0.0963733196258544921875, 0.1270925998687744140625, -0.183977603912353515625, 0.0065670013427734375, 0.0805852413177490234375, 0.137215137481689453125, 0.0680921077728271484375, 0.0806710720062255859375, 0.049774646759033203125},{-0.0064504146575927734375, -0.0220520496368408203125, -0.053467273712158203125, 0.04017353057861328125, -0.0044076442718505859375, 0.06911563873291015625, 0.107593536376953125, -0.13604640960693359375, 0.0553977489471435546875}},
	{{0.12296772003173828125, 0.0988922119140625, -0.033000469207763671875, -0.0307939052581787109375, 0.01214504241943359375, 0.1317188739776611328125, 0.1612186431884765625, -0.089073657989501953125, -0.0592401027679443359375},{0.032182216644287109375, 0.1342213153839111328125, 0.080265045166015625, -0.096942901611328125, -0.0160210132598876953125, 0.06717967987060546875, -0.074717998504638671875, -0.0874722003936767578125, 0.0168211460113525390625},{-0.16569805145263671875, -0.024799823760986328125, -0.2109339237213134765625, -0.050346851348876953125, 0.0349848270416259765625, -0.051171779632568359375, 0.0933854579925537109375, 0.038549900054931640625, 0.1286895275115966796875},{-0.084833621978759765625, -0.122896671295166015625, -0.1836488246917724609375, -0.0633289813995361328125, -0.0063934326171875, 0.028357982635498046875, -0.1088521480560302734375, -0.0269901752471923828125, 0.0399322509765625}},
	{{0.1553974151611328125, 0.19726657867431640625, -0.028252124786376953125, -0.035315990447998046875, 0.0606505870819091796875, 0.1833159923553466796875, 0.0224831104278564453125, -0.0893127918243408203125, 0.084010601043701171875},{0.005339145660400390625, 0.0671927928924560546875, -0.0070450305938720703125, -0.1149065494537353515625, -0.0284173488616943359375, 0.1082160472869873046875, -0.008388042449951171875, -0.0875332355499267578125, 0.0179607868194580078125},{-0.0686900615692138671875, -0.225019931793212890625, 0.0696589946746826171875, -0.0046579837799072265625, -0.021209239959716796875, -0.050169467926025390625, -0.2538588047027587890625, -0.0834109783172607421875, -0.0859973430633544921875},{-0.0117614269256591796875, 0.000165462493896484375, -0.102124691009521484375, 0.0190203189849853515625, -0.00069904327392578125, -0.1840384006500244140625, -0.04227161407470703125, -0.1353442668914794921875, 0.193248271942138671875}},
	{{0.183228015899658203125, 0.08310985565185546875, 0.0273609161376953125, -0.0118887424468994140625, -0.029379367828369140625, 0.073574066162109375, -0.11512088775634765625, -0.0940296649932861328125, -0.1183054447174072265625},{0.132084369659423828125, 0.06356334686279296875, 0.132234096527099609375, -0.0569732189178466796875, 0.066659450531005859375, 0.161017894744873046875, 0.08229732513427734375, 0.0062854290008544921875, -0.0332443714141845703125},{0.0708839893341064453125, -0.0809772014617919921875, -0.2673323154449462890625, -0.157477855682373046875, -0.137462139129638671875, -0.23175907135009765625, -0.0758416652679443359375, -0.2826402187347412109375, -0.02509212493896484375},{-0.01947689056396484375, -0.2088801860809326171875, -0.1253640651702880859375, 0.029752254486083984375, 0.0655453205108642578125, 0.1710681915283203125, -0.0145180225372314453125, 0.0107872486114501953125, 0.0548975467681884765625}},
	{{0.060019016265869140625, -0.0947511196136474609375, -0.0252125263214111328125, -0.092500209808349609375, 0.0235555171966552734375, 0.001590728759765625, -0.199395656585693359375, -0.058414936065673828125, -0.019014835357666015625},{0.1007792949676513671875, -0.01966953277587890625, 0.0908019542694091796875, -0.0810668468475341796875, 0.0384566783905029296875, -0.03497028350830078125, -0.0743710994720458984375, -0.1792781352996826171875, 0.02083301544189453125},{0.07769775390625, -0.241489887237548828125, -0.01339054107666015625, 0.034404754638671875, -0.0165157318115234375, 0.0205566883087158203125, -0.0385525226593017578125, -0.075632572174072265625, -0.1315729618072509765625},{0.0425837039947509765625, -0.183718204498291015625, 0.2481899261474609375, -0.132614612579345703125, 0.0232980251312255859375, -0.073607921600341796875, 0.1418740749359130859375, 0.0754339694976806640625, 0.2587645053863525390625}},
	{{0.1076314449310302734375, 0.0079975128173828125, 0.0583622455596923828125, -0.0466883182525634765625, 0.0751883983612060546875, 0.0273754596710205078125, 0.1019458770751953125, -0.0844299793243408203125, -0.0066034793853759765625},{0.0182168483734130859375, -0.4673731327056884765625, -0.29185009002685546875, -0.1559269428253173828125, 0.06897830963134765625, 0.0619633197784423828125, 0.1198241710662841796875, -0.0172901153564453125, 0.1524426937103271484375},{0.1529533863067626953125, 0.0853493213653564453125, 0.38957977294921875, -0.088122844696044921875, -0.0766956806182861328125, -0.177642345428466796875, -0.0776131153106689453125, -0.04022884368896484375, -0.0107929706573486328125},{0.1463558673858642578125, 0.0124094486236572265625, -0.07830715179443359375, -0.26101589202880859375, -0.114148616790771484375, -0.060604572296142578125, 0.050667285919189453125, -0.1349761486053466796875, -0.108459949493408203125}}},
	{{{0.093442440032958984375, 0.0490481853485107421875, 0.088497638702392578125, 0.1045017242431640625, 0.0784854888916015625, 0.1769771575927734375, 0.3265736103057861328125, 0.18426609039306640625, 0.1442203521728515625},{0.0419075489044189453125, 0.1175954341888427734375, 0.021527767181396484375, 0.0332548618316650390625, -0.006222248077392578125, 0.1441485881805419921875, 0.1077473163604736328125, -0.0785520076751708984375, -0.2126781940460205078125},{-0.3223931789398193359375, -0.5055534839630126953125, -0.21227264404296875, -0.1175625324249267578125, -0.213533878326416015625, 0.090538501739501953125, 0.240111827850341796875, -0.1354897022247314453125, 0.1148674488067626953125},{-0.0889270305633544921875, 0.061804294586181640625, 0.155974864959716796875, -0.0076367855072021484375, -0.103796482086181640625, -0.219456195831298828125, 0.0446212291717529296875, 0.05620098114013671875, -0.1909182071685791015625}},
	{{-0.0100481510162353515625, 0.0809495449066162109375, -0.092316150665283203125, 0.043052196502685546875, 0.02169895172119140625, 0.2292754650115966796875, 0.14490604400634765625, 0.094933986663818359375, -0.05501842498779296875},{0.0226328372955322265625, 0.12782573699951171875, 0.0399153232574462890625, 0.038750171661376953125, -0.026355743408203125, -0.055080890655517578125, 0.0603187084197998046875, -0.19745731353759765625, -0.031180858612060546875},{-0.1387169361114501953125, -0.032855987548828125, 0.2567408084869384765625, 0.080911159515380859375, -0.0617921352386474609375, -0.105975627899169921875, 0.038078784942626953125, 0.1143453121185302734375, -0.138068675994873046875},{0.057048320770263671875, -0.027681827545166015625, 0.030244350433349609375, -0.1159636974334716796875, 0.0495612621307373046875, -0.1611788272857666015625, -0.023494243621826171875, -0.079011440277099609375, 0.1451761722564697265625}},
	{{-0.152937412261962890625, -0.016991138458251953125, -0.08442020416259765625, -0.14289951324462890625, 0.0147264003753662109375, 0.15779972076416015625, 0.3055837154388427734375, -0.0710041522979736328125, 0.1166322231292724609375},{-0.0159969329833984375, 0.094804286956787109375, -0.050165653228759765625, -0.0420124530792236328125, -0.050931453704833984375, 0.034844875335693359375, -0.116046428680419921875, -0.113231182098388671875, -0.16271686553955078125},{-0.0538785457611083984375, -0.0627753734588623046875, 0.082300662994384765625, -0.127369403839111328125, 0.065159320831298828125, -0.1182510852813720703125, 0.0560572147369384765625, -0.0305335521697998046875, 0.145117282867431640625},{0.066082477569580078125, -0.089026927947998046875, -0.236402988433837890625, 0.10380268096923828125, 0.0367658138275146484375, -0.2048161029815673828125, -0.040354251861572265625, -0.0965750217437744140625, 0.156880855560302734375}},
	{{0.006741046905517578125, -0.0007097721099853515625, 0.081638813018798828125, -0.0567114353179931640625, 0.0115711688995361328125, 0.158405780792236328125, -0.0254695415496826171875, -0.0189149379730224609375, -0.0173594951629638671875},{-0.068294525146484375, 0.1481037139892578125, -0.167599201202392578125, 0.029981136322021484375, -0.05587482452392578125, -0.0780341625213623046875, 0.030053615570068359375, -0.174007892608642578125, 0.0781457424163818359375},{0.0141308307647705078125, -0.048910617828369140625, -0.2492001056671142578125, 0.190667629241943359375, 0.035632610321044921875, -0.166924953460693359375, -0.07090091705322265625, -0.006240367889404296875, 0.138824462890625},{-0.1911027431488037109375, -0.2299091815948486328125, -0.44995975494384765625, 0.04379177093505859375, -0.028957843780517578125, -0.016411304473876953125, 0.0158827304840087890625, 0.233297824859619140625, 0.1749861240386962890625}},
	{{0.076097965240478515625, 0.0273387432098388671875, -0.2020175457000732421875, -0.07595920562744140625, -0.092121124267578125, -0.133623600006103515625, 0.0534384250640869140625, -0.1305811405181884765625, 0.029035091400146484375},{0.0546720027923583984375, 0.22876071929931640625, 0.1419804096221923828125, 0.1915900707244873046875, 0.1181337833404541015625, 0.1097633838653564453125, -0.11330509185791015625, -0.09873294830322265625, -0.0384273529052734375},{-0.1358444690704345703125, -0.239682674407958984375, -0.4888846874237060546875, -0.117208003997802734375, -0.0981581211090087890625, -0.06147003173828125, -0.149160861968994140625, -0.061380863189697265625, 0.111874103546142578125},{-0.221196651458740234375, -0.217284679412841796875, 0.0646402835845947265625, 0.2401549816131591796875, 0.0897824764251708984375, -0.0181934833526611328125, -0.0294425487518310546875, 0.1713554859161376953125, 0.1607372760772705078125}},
	{{0.1059324741363525390625, 0.0573940277099609375, 0.1309802532196044921875, 0.0370347499847412109375, 0.0514202117919921875, 0.058502197265625, -0.004030704498291015625, -0.02115726470947265625, -0.0649898052215576171875},{0.10644435882568359375, 0.13549518585205078125, 0.3392810821533203125, -0.050776958465576171875, 0.0318152904510498046875, -0.0828113555908203125, 0.0373761653900146484375, 0.040672779083251953125, 0.2525589466094970703125},{-0.2665717601776123046875, -0.269592761993408203125, 0.04843807220458984375, 0.056252002716064453125, -0.01996326446533203125, -0.065372943878173828125, -0.0948145389556884765625, -0.074769496917724609375, -0.144232273101806640625},{-0.37279796600341796875, -0.485281467437744140625, 0.1513741016387939453125, -0.146758556365966796875, -0.024086475372314453125, -0.37051868438720703125, -0.0457251071929931640625, 0.038651943206787109375, 0.0813236236572265625}},
	{{0.157402515411376953125, 0.2985990047454833984375, 0.1802237033843994140625, 0.02737140655517578125, -0.002029895782470703125, -0.1548511981964111328125, 0.0635929107666015625, 0.1027443408966064453125, 0.14031982421875},{0.1507413387298583984375, 0.228121280670166015625, 0.032693386077880859375, -0.104747772216796875, -0.080452442169189453125, -0.16116809844970703125, -0.0958995819091796875, -0.0021612644195556640625, -0.0088589191436767578125},{-0.2566907405853271484375, -0.257682323455810546875, 0.359356403350830078125, -0.03969669342041015625, -0.0547826290130615234375, -0.185234546661376953125, -0.065794467926025390625, -0.018648624420166015625, -0.033547878265380859375},{-0.143035411834716796875, -0.0083160400390625, 0.17058849334716796875, -0.1721794605255126953125, -0.2171494960784912109375, -0.3754231929779052734375, -0.5502097606658935546875, -0.295653820037841796875, -0.0011196136474609375}}}};
*/
	/*cout << "const weight WeightData[WINDOW_BLOCKNUM_H][WINDOW_BLOCKNUM_W] = {" << endl;
	for(int i = 0; i < WINDOW_BLOCKNUM_H; i++){
		cout << "{";
		for(int j = 0; j < WINDOW_BLOCKNUM_W; j++){
			cout << "{";
			weight w = WeightData[i][j];
			cout << "{";
			for(int k = 0; k < 9; k++){
				cout << w.upperleft.data[k].to_string(10);
				if(k != 8) cout << ", ";
			}
			cout << "},";
			cout << "{";
			for(int k = 0; k < 9; k++){
				cout << w.upperright.data[k].to_string(10);
				if(k != 8) cout << ", ";
			}
			cout << "},";
			cout << "{";
			for(int k = 0; k < 9; k++){
				cout << w.bottomleft.data[k].to_string(10);
				if(k != 8) cout << ", ";
			}
			cout << "},";
			cout << "{";
			for(int k = 0; k < 9; k++){
				cout << w.bottomright.data[k].to_string(10);
				if(k != 8) cout << ", ";
			}
			cout << "}";
			cout << "}";
			if(j != WINDOW_BLOCKNUM_W - 1) cout << ", " << endl;
		}
		cout << "}";
		if(i !=  WINDOW_BLOCKNUM_H - 1) cout << ", " << endl;
	}
	cout << "};" << endl;*/
//#pragma HLS ARRAY_PARTITION variable=WeightData complete dim=1
//#pragma HLS RESOURCE variable=WeightData core=ROM_1P_BRAM
	accum_fixed PartialSum[WINDOW_BLOCKNUM_H][WINDOW_NUM_W];
#pragma HLS ARRAY_PARTITION variable=PartialSum complete dim=1
#pragma HLS RESOURCE variable=PartialSum core=RAM_2P_BRAM

	for(int i = 0; i < WINDOW_BLOCKNUM_H; i++){
		for(int j = 0; j < WINDOW_NUM_W; j++){
#pragma HLS PIPELINE II=1
			PartialSum[i][j] = 0;
		}
	}
	loop_y:for(int y = 0; y < BLOCK_NUM_H; y++){
		loop_x:for(int x = 0; x < BLOCK_NUM_W; x++){
			ap_fixed9_float ul = upperleft.read();
			ap_fixed9_float ur = upperright.read();
			ap_fixed9_float bl = bottomleft.read();
			ap_fixed9_float br = bottomright.read();
//#pragma HLS PIPELINE II=1
			for(int block_index_x = 6; block_index_x >= 0; block_index_x--){
#pragma HLS PIPELINE II=1
				bool inside_window = (block_index_x <= x && x <= block_index_x + (IMAGE_WIDTH / 8 - 8));
				if(inside_window){
					int winx = x - block_index_x;
					//block_index_y indicates where (ul,ur,bl,br) is located in the window in y axis.
					loop_block_index_y:for(int block_index_y = 0; block_index_y < WINDOW_BLOCKNUM_H; block_index_y++){
						int block_start_y = y - block_index_y;
						if(0 <= block_start_y && block_start_y <= (BLOCK_NUM_H - WINDOW_BLOCKNUM_H)){
							int partial_sum_index_y = (y - block_index_y) % WINDOW_BLOCKNUM_H;
							hogweight w;// = WeightData[block_index_y][block_index_x];
							if(block_index_y == 0) w = w1[block_index_x];
							else if(block_index_y == 1) w = w2[block_index_x];
							else w = w3[block_index_x];
#pragma HLS allocation instances=multiply_accum_hog limit=2
							accum_fixed tmp_partial_sum = 0;
							for(int i = 0; i < 9; i++) tmp_partial_sum += multiply_accum_hog(w.weightval[i], ul.data[i], ur.data[i], bl.data[i], br.data[i]);
							if(block_index_y == 0 && block_index_x == 0) PartialSum[partial_sum_index_y][winx] = tmp_partial_sum;
							else PartialSum[partial_sum_index_y][winx] += tmp_partial_sum;

							bool window_completed = (block_index_x == (WINDOW_BLOCKNUM_W - 1) && block_index_y == (WINDOW_BLOCKNUM_H - 1));
							if(window_completed){
								accum_fixed allsum = PartialSum[partial_sum_index_y][winx];
								//PartialSum[partial_sum_index_y][winx] = 0;
								//ap_axis<8,1,1,1> ap_y, ap_x;
								//ap_y.data = block_start_y;
								//ap_x.data = winx;
								resultstream.write(allsum);
							}
						}
					}
				}
			}
		}
	}

}


void hog_svm_part(hls::stream<ap_axiu<32,1,1,1> >& instream, hls::stream<ap_axiu<32,1,1,1> >& outstream,
		pixweight bgrhsv_w1[8], pixweight bgrhsv_w2[8], pixweight bgrhsv_w3[8], pixweight bgrhsv_w4[8],
		hogweight hog_w1[7], hogweight hog_w2[7], hogweight hog_w3[7]){

	hls::stream<bgr>upper_scaled_rgb, bottom_scaled_rgb;
	hls::stream<ap_uint<8> > gray_pix;
	hls::stream<magnitude_fixed > magstream;
	hls::stream<int> binstream;
	hls::stream<blockpart_fixed_9 > bottom, upper;
	hls::stream<ap_fixed9_float > ul_out, ur_out, bl_out, br_out;
	hls::stream<accum_fixed> hog_resultstream, bgr_hsv_resultstream;
#pragma HLS INTERFACE axis port=instream
#pragma HLS INTERFACE axis port=outstream
#pragma HLS INTERFACE bram port=bgrhsv_w1
#pragma HLS INTERFACE bram port=bgrhsv_w2
#pragma HLS INTERFACE bram port=bgrhsv_w3
#pragma HLS INTERFACE bram port=bgrhsv_w4
#pragma HLS INTERFACE bram port=hog_w1
#pragma HLS INTERFACE bram port=hog_w2
#pragma HLS INTERFACE bram port=hog_w3
#pragma HLS RESOURCE variable=bgrhsv_w1 core=RAM_1P_BRAM
#pragma HLS RESOURCE variable=bgrhsv_w2 core=RAM_1P_BRAM
#pragma HLS RESOURCE variable=bgrhsv_w3 core=RAM_1P_BRAM
#pragma HLS RESOURCE variable=bgrhsv_w4 core=RAM_1P_BRAM
#pragma HLS RESOURCE variable=hog_w1 core=RAM_1P_BRAM
#pragma HLS RESOURCE variable=hog_w2 core=RAM_1P_BRAM
#pragma HLS RESOURCE variable=hog_w3 core=RAM_1P_BRAM
#pragma HLS INTERFACE s_axilite port=return     bundle=CONTROL_BUS
#pragma HLS DATAFLOW
#pragma HLS STREAM variable = bgr_hsv_resultstream depth = 100 dim = 1
	grayscale_and_resizing(instream, gray_pix, upper_scaled_rgb, bottom_scaled_rgb);
	compute_mag_and_bin(gray_pix, magstream, binstream);
	cell_histogram_generate(magstream, binstream, bottom, upper);
	block_histogram_normalization(bottom, upper, ul_out, ur_out, bl_out, br_out);
	hog_svm_classification(ul_out, ur_out, bl_out, br_out, hog_resultstream, hog_w1, hog_w2, hog_w3);
	bgr_hsv_svm_classification(upper_scaled_rgb, bottom_scaled_rgb, bgr_hsv_resultstream, bgrhsv_w1, bgrhsv_w2, bgrhsv_w3, bgrhsv_w4);
	int outputnum = 27*33;//4161;
	accum_fixed bias = -1.7700042;
	for(int i = 0; i < outputnum; i++){
		accum_fixed hog = hog_resultstream.read();
		accum_fixed bgr_hsv = bgr_hsv_resultstream.read();//bgr_hsv_result[i];
		accum_fixed bined = hog + bgr_hsv + bias;
		float final_rst_float = bined.to_float();
		//float final_rst_float = bgr_hsv.to_float();
		//cout << fixed << setprecision(10) << final_rst_float << endl;
		ap_axiu<32,1,1,1> val;
		union{
			int oval;
			float ival;
		} converter;
		converter.ival = final_rst_float;
		val.data = converter.oval;
		val.last = (i == outputnum-1) ? 1 : 0;
		val.strb = -1;
		val.keep = 15;
		val.user = 0;
		val.id = 0;
		val.dest = 0;
		outstream.write(val);
	}
}
