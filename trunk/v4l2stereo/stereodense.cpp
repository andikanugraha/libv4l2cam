/*
    Dense stereo correspondence
    For a description of this algorithm see:
        An Introduction to 3D Computer Vision Techniques and Algorithms,
        Buguslaw Cyganek & J. Paul Siebert,
        ISBN 978-0-470-01704-3
        Section 6.6.4
    Copyright (C) 2010 Bob Mottram
    fuzzgun@gmail.com

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "stereodense.h"

/*!
 * \brief removes speckling from the disparity map
 * \param disparity_map_width width of the disparity map
 * \param disparity_map_height height of the disparity map
 * \param disparity_map disparity map data
 * \param max_disparity_pixels maximum disparity in pixels
 */
void stereodense::despeckle_disparity_map(
	int disparity_map_width,
	int disparity_map_height,
	unsigned int* disparity_map,
	int max_disparity_pixels)
{
	unsigned int threshold = (unsigned int)(max_disparity_pixels*70/100);
	unsigned int min_diff = (unsigned int)(max_disparity_pixels*5/100);
	const int hits_threshold = 5;
    #pragma omp parallel for
	for (int y = 1; y < disparity_map_height-1; y++) {
		for (int x = 1; x < disparity_map_width-1; x++) {
			int n_map = (y*disparity_map_width + x)*2;
            if (disparity_map[n_map+1] > threshold) {
            	int hits = 0;
            	unsigned int centre_disparity = disparity_map[n_map+1];
            	for (int yy = y-1; yy <= y+1; yy++) {
            		int n_map2 = (yy*disparity_map_width + x - 1)*2;
            		for (int xx = x-1; xx <= x+1; xx++, n_map2 += 2) {
            			unsigned int diff = centre_disparity - disparity_map[n_map2+1];
            			if (diff > min_diff) {
            				hits++;
            				if (hits > hits_threshold) {
            					yy = y+2;
            					break;
            				}
            			}
            		}
            	}
            	if (hits > hits_threshold) {
            		disparity_map[n_map] = 0;
            	}
            }
		}
	}

	for (int i = disparity_map_width*disparity_map_height*2-2; i >= 0; i -= 2) {
		if (disparity_map[i] == 0) {
			disparity_map[i + 1] = 0;
		}
	}
}

/*!
 * \brief returns the mean red/green/blue values for the given image row
 * \param img colour image data
 * \param img_width width of the image
 * \param y row
 * \param mean_r returned mean red value
 * \param mean_g returned mean green value
 * \param mean_b returned mean blue value
 * \param mean_r_deviation returned red standard deviation
 * \param mean_g_deviation returned green standard deviation
 * \param mean_b_deviation returned blue standard deviation
 */
void stereodense::mean_row_reflectance(
	unsigned char* img,
	int img_width,
	int y,
	int &mean_r,
	int &mean_g,
	int &mean_b,
    int &mean_r_deviation,
    int &mean_g_deviation,
    int &mean_b_deviation)
{
	mean_r = 0;
	mean_g = 0;
	mean_b = 0;

	int n = y*img_width*3;
	for (int x = 0; x < img_width; x++, n += 3) {
        mean_r += img[n + 2];
        mean_g += img[n + 1];
        mean_b += img[n];
	}
	mean_r /= img_width;
	mean_g /= img_width;
	mean_b /= img_width;

	mean_r_deviation = 0;
	mean_g_deviation = 0;
	mean_b_deviation = 0;

	n = y*img_width*3;
	for (int x = 0; x < img_width; x++, n += 3) {
        mean_r_deviation += abs(img[n + 2] - mean_r);
        mean_g_deviation += abs(img[n + 1] - mean_g);
        mean_b_deviation += abs(img[n] - mean_b);
	}
	mean_r_deviation /= img_width;
	mean_g_deviation /= img_width;
	mean_b_deviation /= img_width;
}

/*!
 * \brief performs colour correction on the right image.  This helps to improve matching performance.
 * \param img_left left colour image
 * \param img_right right colour image
 * \param img_width width of the image
 * \param img_height height of the image
 * \param offset_y calibration y offset
 */
void stereodense::colour_correction(
	unsigned char* img_left,
	unsigned char* img_right,
	int img_width,
	int img_height,
	int offset_y)
{
    #pragma omp parallel for
	for (int y_left = 0; y_left < img_height; y_left++) {
		int y_right = y_left - offset_y;
		if ((y_right > -1) && (y_right < img_height)) {

			// get the mean and standard deviation for the left image
			int mean_r_left=0;
			int mean_g_left=0;
			int mean_b_left=0;
			int mean_r_deviation_left=0;
			int mean_g_deviation_left=0;
			int mean_b_deviation_left=0;

			mean_row_reflectance(
				img_left,
				img_width,
				y_left,
				mean_r_left,
				mean_g_left,
				mean_b_left,
				mean_r_deviation_left,
				mean_g_deviation_left,
				mean_b_deviation_left);

			// get the mean and standard deviation for the right image
			int mean_r_right=0;
			int mean_g_right=0;
			int mean_b_right=0;
			int mean_r_deviation_right=0;
			int mean_g_deviation_right=0;
			int mean_b_deviation_right=0;

			mean_row_reflectance(
				img_right,
				img_width,
				y_right,
				mean_r_right,
				mean_g_right,
				mean_b_right,
				mean_r_deviation_right,
				mean_g_deviation_right,
				mean_b_deviation_right);

			// correct the colours of the right image so that they're similar to the left
			if ((mean_r_deviation_right != 0) &&
				(mean_g_deviation_right != 0) &&
				(mean_b_deviation_right != 0)) {
				int n = y_right*img_width*3;
				for (int x = 0; x < img_width; x++, n += 3) {
					img_right[n+2] = (unsigned char)(mean_r_left + ((img_right[n+2] - mean_r_right) *  mean_r_deviation_left / mean_r_deviation_right));
					img_right[n+1] = (unsigned char)(mean_g_left + ((img_right[n+1] - mean_g_right) *  mean_g_deviation_left / mean_g_deviation_right));
					img_right[n] = (unsigned char)(mean_b_left + ((img_right[n] - mean_b_right) *  mean_b_deviation_left / mean_b_deviation_right));
				}
			}
		}
	}
}

/*!
 * \brief returns the sum of absolute differences for two image patches
 * \param img_left left colour image
 * \param img_right right colour image
 * \param img_width width of the image
 * \param img_height height of the image
 * \param x_left x centre coordinate for the left image patch
 * \param y_left y centre coordinate for the left image patch
 * \param x_right x centre coordinate for the right image patch
 * \param y_right y centre coordinate for the right image patch
 * \param radius radius of the patch
 * \return sum of absolute differences
 */
int stereodense::SAD(
	unsigned char* img_left,
	unsigned char* img_right,
	int img_width,
	int img_height,
	int x_left,
	int y_left,
	int x_right,
	int y_right,
	int radius)
{
	int sad = -1;

	if ((x_left-radius > -1) && (x_right-radius > -1) &&
		(x_left+radius < img_width-1) && (x_right+radius < img_width-1) &&
		(y_left-radius > -1) && (y_right-radius > -1) &&
		(y_left+radius < img_height-1) && (y_right+radius < img_height-1)) {

		sad = 0;
		for (int dy = -radius; dy <= radius; dy++) {
			int n_left = ((y_left + dy)*img_width + x_left - radius)*3;
			int n_right = ((y_right + dy)*img_width + x_right - radius)*3;
			for (int dx = -radius; dx <= radius; dx++, n_left += 3, n_right += 3) {
			    sad += abs(img_left[n_left] - img_right[n_right]) +
			           abs(img_left[n_left+1] - img_right[n_right+1]) +
			           abs(img_left[n_left+2] - img_right[n_right+2]);
			}
		}
	}
	return(sad);
}

/*!
 * \brief checks the given disparity by comparing pixels
 * \param x disparity map x coordinate
 * \param y disparity map y coordinate
 * \param disparity possible disparity in pixels
 * \param similarity_threshold maximum pixel difference
 * \param img_left left colour image
 * \param img_right right colour image
 * \param img_width width of the image
 * \param img_height height of the image
 * \param offset_x calibration x offset
 * \param offset_y calibration y offset
 * \param smoothing_radius smoothing radius for the disparity space
 * \param vertical_sampling vertical sampling rate
 */
bool stereodense::cross_check_pixel(
	int x,
	int y,
	int disparity,
	int similarity_threshold,
	unsigned char* img_left,
	unsigned char* img_right,
	int img_width,
	int img_height,
	int offset_x,
	int offset_y,
	int smoothing_radius,
	int vertical_sampling)
{
	bool check_ok = false;

	// cross check pixel intensity values
	int y_left = y*STEREO_DENSE_SMOOTH_VERTICAL*vertical_sampling;
	int y_right = (y*STEREO_DENSE_SMOOTH_VERTICAL*vertical_sampling) - offset_y;
	int x_left = x*smoothing_radius;
	int x_right = x_left - disparity - offset_x;
	int stride = img_width*3*2;
	if ((x_right > 2) && (x_right < img_width-2) &&
		(y_left < img_height-2) && (y_right < img_height-2)) {

		int n_left = (y_left*img_width + x_left)*3;
		int n_right = (y_right*img_width + x_right)*3;
		for (int samples = 0; samples < 3; samples++) {
			check_ok = false;
			if (abs(img_left[n_left] - img_right[n_right]) +
				abs(img_left[n_left+1] - img_right[n_right+1]) +
				abs(img_left[n_left+2] - img_right[n_right+2]) <
				similarity_threshold) {
				if (abs(img_left[n_left-stride] - img_right[n_right-stride]) +
					abs(img_left[n_left+1-stride] - img_right[n_right+1-stride]) +
					abs(img_left[n_left+2-stride] - img_right[n_right+2-stride]) <
					similarity_threshold) {
					if (abs(img_left[n_left+stride] - img_right[n_right+stride]) +
						abs(img_left[n_left+1+stride] - img_right[n_right+1+stride]) +
						abs(img_left[n_left+2+stride] - img_right[n_right+2+stride]) <
						similarity_threshold) {
						check_ok = true;
					}
				}
			}
			if (!check_ok) break;
			n_left += 3;
			n_right += 3;
		}
	}
    return(check_ok);
}

/*!
 * \brief generates a disparity map from the disparity space
 * \param img_left left colour image
 * \param img_right right colour image
 * \param img_width width of the image
 * \param img_height height of the image
 * \param offset_x calibration x offset
 * \param offset_y calibration y offset
 * \param smoothing_radius smoothing radius for the disparity space
 * \param vertical_sampling vertical sampling rate
 * \param disparity_space disparity space containing correlation data for each pixel at each possible disparity
 * \param disparity_space_width width of the disparity space
 * \param disparity_space_height height of the disparity space
 * \param disparity_step disparity step size
 * \param no_of_disparities number of disparities within the disparity space
 * \param similarity_threshold maximum pixel difference when cross checking
 * \param disparity_map returned disparity map
 */
void stereodense::disparity_map_from_disparity_space(
	unsigned char* img_left,
	unsigned char* img_right,
	int img_width,
	int img_height,
	int offset_x,
	int offset_y,
	int smoothing_radius,
	int vertical_sampling,
	unsigned int* disparity_space,
	int disparity_space_width,
	int disparity_space_height,
	int disparity_step,
	int no_of_disparities,
	int similarity_threshold,
	unsigned int* disparity_map)
{
	int disparity_space_pixels = disparity_space_width*disparity_space_height;
	int disparity_space_width2 = disparity_space_width/2;

	// clear the disparity map
	memset((void*)disparity_map,'\0',disparity_space_pixels*2*sizeof(unsigned int));

	// process each disparity in parallel
	for (int disparity_index = 0; disparity_index < no_of_disparities; disparity_index++) {
		int disparity_space_offset = disparity_index*disparity_space_pixels*2;

		// for every pixel at this disparity
        #pragma omp parallel for
		for (int y = 1; y < disparity_space_height-1; y++) {
			int y2 = y/2;
			int n_map = (y*disparity_space_width + 1)*2;
			int n_space_inner = disparity_space_offset + (y*disparity_space_width) + 1;
			for (int x = 1; x < disparity_space_width-1; x++, n_map += 2, n_space_inner++) {

				int n_space_outer = disparity_space_pixels + disparity_space_offset + (y2*disparity_space_width2) + (x/2);

				// small correlation window
				unsigned int local_correlation_inner =
					disparity_space[n_space_inner] +
					disparity_space[n_space_inner-1] +
					disparity_space[n_space_inner+1] +
					disparity_space[n_space_inner-disparity_space_width] +
					disparity_space[n_space_inner+disparity_space_width] +
					disparity_space[n_space_inner+disparity_space_width-1] +
					disparity_space[n_space_inner+disparity_space_width+1] +
					disparity_space[n_space_inner-disparity_space_width-1] +
					disparity_space[n_space_inner-disparity_space_width+1];

				// large correlation window
				unsigned int local_correlation_outer =
					disparity_space[n_space_outer] +
					disparity_space[n_space_outer-1] +
					disparity_space[n_space_outer+1] +
					disparity_space[n_space_outer-disparity_space_width2] +
					disparity_space[n_space_outer+disparity_space_width2] +
					disparity_space[n_space_outer+disparity_space_width2-1] +
					disparity_space[n_space_outer+disparity_space_width2+1] +
					disparity_space[n_space_outer-disparity_space_width2-1] +
					disparity_space[n_space_outer-disparity_space_width2+1];

				// combined correlation value
				// more emphasis on the centre, less on the surround
				unsigned int local_correlation = (local_correlation_inner*4) + local_correlation_outer;

				// update the disparity map
				if ((local_correlation > 0) && ((disparity_map[n_map] == 0) ||
					(disparity_map[n_map] < local_correlation))) {

					for (int tries = 0; tries < 8; tries++) {

						int disparity = (disparity_index*disparity_step) + tries;

						if (cross_check_pixel(
							x,
							y,
							disparity,
							similarity_threshold,
							img_left,
							img_right,
							img_width,
							img_height,
							offset_x,
							offset_y,
							smoothing_radius,
							vertical_sampling)) {

							if (cross_check_pixel(
								x,
								y+1,
								disparity,
								similarity_threshold,
								img_left,
								img_right,
								img_width,
								img_height,
								offset_x,
								offset_y,
								smoothing_radius,
								vertical_sampling)) {

								if (cross_check_pixel(
									x,
									y-1,
									disparity,
									similarity_threshold,
									img_left,
									img_right,
									img_width,
									img_height,
									offset_x,
									offset_y,
									smoothing_radius,
									vertical_sampling)) {

									disparity_map[n_map] = local_correlation;
									disparity_map[n_map + 1] = disparity;
									break;
								}
							}
						}
					}
				}

			}
		}
	}

}

/*!
 * \brief updates the disparity space which contains matching correlation values for each possible disparity
 * \param img_left colour data for the left image2
 * \param img_right colour data for the right image
 * \param img_width width of the image
 * \param img_height height of the image
 * \param offset_x calibration offset x
 * \param offset_y calibration offset y
 * \param vertical_sampling vertical sampling rate - we don't need every row
 * \param max_disparity_percent maximum disparity as a percentage of image width
 * \param correlation_radius radius in pixels used for patch matching
 * \param smoothing_radius radius in pixels used for smoothing of the disparity space
 * \param disparity_step step size for sampling different disparities
 * \param disparity_space_width width of the disparity space
 * \param disparity_space_height height of the disparity space
 * \param disparity_space array used for the disparity space
 */
void stereodense::update_disparity_space(
	unsigned char* img_left,
	unsigned char* img_right,
	int img_width,
	int img_height,
	int offset_x,
	int offset_y,
	int vertical_sampling,
	int max_disparity_percent,
	int correlation_radius,
	int smoothing_radius,
	int disparity_step,
	int disparity_space_width,
	int disparity_space_height,
	unsigned int *disparity_space)
{
	int patch_pixels = correlation_radius*2+1;
	patch_pixels *= patch_pixels;
	unsigned int max_patch_value = (unsigned int)(3*255*patch_pixels);
	int max_disparity = max_disparity_percent * img_width / 100;

	int img_height2 = img_height / vertical_sampling;
	int width2 = img_width/smoothing_radius;
	int height2 = img_height2/STEREO_DENSE_SMOOTH_VERTICAL;
	int width3 = img_width/(smoothing_radius*2);

	int ty = 0;
	int by = img_height;
	if (offset_y >= 0)
		by = img_height - offset_y;
	else
		ty = -offset_y;

	int disparity_space_pixels = disparity_space_width*disparity_space_height;
    int no_of_disparities = max_disparity / disparity_step;

	// clear disparity space
	memset((void*)disparity_space,'\0',no_of_disparities*disparity_space_pixels*2*sizeof(unsigned int));

	// test a number of possible disparities in parallel
    #pragma omp parallel for
	for (int disparity_index = 0; disparity_index < no_of_disparities; disparity_index++) {

		// disparity in pixels
	    int disparity = disparity_index * disparity_step;

	    // offset within the disparity space array
		int disparity_space_offset = disparity_index*disparity_space_pixels*2;

		// insert correlation values into the disparity space
		int y2 = 0;
		for (int y = ty; y < by; y += vertical_sampling, y2++) {

			int yy = y2 / STEREO_DENSE_SMOOTH_VERTICAL;
			if ((yy > 1) && (yy < height2-2)) {

				int yy2 = yy/2;
				int x_right = -offset_x - disparity;

				// for all pixels along the row
				for (int x_left = 0; x_left < img_width - offset_x; x_left++, x_right++) {

					if ((x_left - correlation_radius > -1) &&
						(x_left + correlation_radius < img_width) &&
						(x_right - correlation_radius > -1) &&
						(x_right + correlation_radius < img_width)) {

						// difference between patches in the left and right images
						int sad =
							SAD(img_left,img_right,img_width,img_height,
								x_left, y,
								x_right, y - offset_y, correlation_radius);

						if (sad > -1) {
							int xx_inner = x_left / smoothing_radius;
							if ((xx_inner > 1) && (xx_inner < width2-2)) {
								int n_inner = (yy*width2 + xx_inner) + disparity_space_offset;
								unsigned int v = max_patch_value - (unsigned int)sad;
							    disparity_space[n_inner] += v;

								int n_outer = (yy2*width3 + (xx_inner/2)) + disparity_space_offset + disparity_space_pixels;
							    disparity_space[n_outer] += v;
							}
						}
					}
				}
			}
		}
	}
}

/*!
 * \brief calculates a disparity map given two images
 * \param img_left colour data for the left image2
 * \param img_right colour data for the right image
 * \param img_width width of the image
 * \param img_height height of the image
 * \param offset_x calibration offset x
 * \param offset_y calibration offset y
 * \param vertical_sampling vertical sampling rate - we don't need every row
 * \param max_disparity_percent maximum disparity as a percentage of image width
 * \param correlation_radius radius in pixels used for patch matching
 * \param smoothing_radius radius in pixels used for smoothing of the disparity space
 * \param disparity_step step size for sampling different disparities
 * \param disparity_threshold_percent a threshold applied to the disparity map
 * \param despeckle optionally apply despeckling to clean up the disparity map
 * \param disparity_space array used for the disparity space
 * \param disparity_map returned disparity map
 */
void stereodense::update_disparity_map(
	unsigned char* img_left,
	unsigned char* img_right,
	int img_width,
	int img_height,
	int offset_x,
	int offset_y,
	int vertical_sampling,
	int max_disparity_percent,
	int correlation_radius,
	int smoothing_radius,
	int disparity_step,
	int disparity_threshold_percent,
	bool despeckle,
	int cross_checking_threshold,
	unsigned int *disparity_space,
	unsigned int *disparity_map)
{
	int disparity_space_width = img_width/smoothing_radius;
	int disparity_space_height = (img_height / vertical_sampling)/STEREO_DENSE_SMOOTH_VERTICAL;
	int max_disparity_pixels = max_disparity_percent * img_width / 100;

	// correct the colours of the right image so that they're similar to the left
	colour_correction(
		img_left,
		img_right,
		img_width,
		img_height,
		offset_y);

	// create the disparity space
	update_disparity_space(
		img_left,
		img_right,
		img_width,
		img_height,
		offset_x,
		offset_y,
		vertical_sampling,
		max_disparity_percent,
		correlation_radius,
		smoothing_radius,
		disparity_step,
		disparity_space_width,
		disparity_space_height,
		disparity_space);

	// create the disparity map
	disparity_map_from_disparity_space(
		img_left,
		img_right,
		img_width,
		img_height,
		offset_x,
		offset_y,
		smoothing_radius,
		vertical_sampling,
		disparity_space,
		disparity_space_width,
		disparity_space_height,
		disparity_step,
		max_disparity_pixels/disparity_step,
		cross_checking_threshold,
		disparity_map);

	// clean up the disparity map
	if (despeckle) {
	    despeckle_disparity_map(
		    disparity_space_width,
		    disparity_space_height,
		    disparity_map,
		    max_disparity_pixels);
	}

	// optionally apply a threshold to the disparity map
	if (disparity_threshold_percent > 0) {
		unsigned int disparity_threshold_pixels = (unsigned int)(disparity_threshold_percent * max_disparity_pixels / 100);
	    for (int i = disparity_space_width*disparity_space_height*2-2; i >= 0; i -= 2) {
		    if (disparity_map[i+1] < disparity_threshold_pixels) {
		    	disparity_map[i+1] = 0;
		    }
	    }
	}

	// multiply disparity values so that sub-pixel interpolation is possible
	for (int i = disparity_space_width*disparity_space_height*2-2; i >= 0; i -= 2) {
		disparity_map[i + 1] *= STEREO_DENSE_SUB_PIXEL;
	}
}

/*!
 * \brief show the disparity map
 * \param img colour image data
 * \param img_width width of the image
 * \param img_height height of the image
 * \param vertical_sampling vertical sampling rate
 * \param smoothing_radius radius in pixels used for disparity space smoothing
 * \param max_disparity_percent maximum disparity as a percentage of image width
 * \param disparity_map disparity map to be shown
 */
void stereodense::show(
	unsigned char* img,
	int img_width,
	int img_height,
	int vertical_sampling,
	int smoothing_radius,
	int max_disparity_percent,
	unsigned int *disparity_map)
{
	int max_disparity_pixels = img_width * max_disparity_percent * STEREO_DENSE_SUB_PIXEL / 100;
	int width2 = img_width/smoothing_radius;

	for (int y = 0; y < img_height; y++) {
		int n2 = ((y/vertical_sampling)/STEREO_DENSE_SMOOTH_VERTICAL)*width2;
		for (int x = 0; x < img_width; x++) {
			int n = ((y*img_width) + x)*3;
			int n2b = (n2 + (x/smoothing_radius))*2;
            unsigned char disparity = (unsigned char)((int)disparity_map[n2b + 1] * 255  / max_disparity_pixels);
            img[n] = disparity;
            img[n+1] = disparity;
            img[n+2] = disparity;
		}
	}
}
