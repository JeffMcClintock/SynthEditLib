#include "helpers/GmpiPluginEditor.h"
#include "half.hpp"
#include <algorithm>

using half_float::half;

using namespace gmpi;
using namespace gmpi::editor;
using namespace gmpi::drawing;

// It's hard to find a clear example of this algorithm online
// since they all have various "crusty" optimizations
// This one is optimized for readability and simplicity
// was 'circularBufferSingleChannel'
static void stackBlur(uint8_t* buffer, unsigned int w, unsigned int h, unsigned int radius)
{
    //const auto w = static_cast<unsigned int> (img.getWidth());
    //const auto h = static_cast<unsigned int> (img.getHeight());
    // juce::Image::BitmapData data(img, juce::Image::BitmapData::readWrite);
	constexpr int pixelStride = 4; // 4 halfs per pixel (RGBA)

    // Ensure radius is within bounds
    radius = std::clamp(radius, 1u, 254u);

    // The "queue" represents the current values within the sliding kernel's radius.
    // Audio people: yes, it's a circular buffer
    // It efficiently handles the sliding window nature of the kernel.
    std::vector<uint8_t> queue((radius * 2) + 1);

    // This tracks the start of the circular buffer
    unsigned int queueIndex = 0;

    // the "stack" is all in our head, maaaan
    // think of it as a *weighted* sum of the values in the queue
    // each time the queue moves, we add the rightmost values of the queue to the stack
    // and remove the left values of the queue from the stack
    // the blurred pixel is then calculated by dividing by the number of "values" in the weighted stack sum
    float stackSum = 0;
    unsigned int sizeOfStack = (radius + 1) * (radius + 1);

    // Sum of values in the right half of the queue
    float sumIn = 0;

    // Sum of values in the left half of the queue
    float sumOut = 0;

    // horizontal pass
    for (auto y = 0u; y < h; ++y)
    {
        auto row = buffer + pixelStride * w * y; // data.getLinePointer(static_cast<int>(y));

        // clear everything
        stackSum = sumIn = sumOut = queueIndex = 0;

        // Pre-fill the left half of the queue with the leftmost pixel value
        for (auto i = 0u; i <= radius; ++i)
        {
            queue[i] = row[0];

            // these init left side AND middle of the stack
            sumOut += row[0];
            stackSum += row[0] * (i + 1);
        }

        // Fill the right half of the queue with the actual pixel values
        // zero is the center pixel here, it's added to the sum radius + 1 times
        for (auto i = 1u; i <= radius; ++i)
        {
            // edge case where queue is bigger than image width!
            // for example vertical test where width = 1
            auto value = (i <= w - 1) ? row[i] : row[w - 1];

            queue[radius + i] = value; // look ma, no bounds checkin'
            sumIn += value;
            stackSum += value * (radius + 1 - i);
        }

        for (auto x = 0u; x < w; ++x)
        {
            // calculate the blurred value from the stack
            row[x] = /*static_cast<unsigned char>*/(stackSum / sizeOfStack);

            // remove the outgoing sum from the stack
            stackSum -= sumOut;

            // remove the leftmost value from sumOut
            sumOut -= queue[queueIndex];

            // Conveniently, after advancing the index of a circular buffer
            // the old "start" (aka queueIndex) will be the new "end",
            // This means we can just replace it with the incoming pixel value
            // if we hit the right edge, use the rightmost pixel value
            auto nextIndex = x + radius + 1;
            if (nextIndex < w)
                queue[queueIndex] = row[nextIndex];
            else
                queue[queueIndex] = row[w - 1];

            // Also add the new incoming value to the sumIn
            sumIn += queue[queueIndex];

            // Advance the queue index by 1 position
            // the new incoming element is now the "last" in the queue
            if (++queueIndex == queue.size())
                queueIndex = 0;

            // Put into place the next incoming sums
            stackSum += sumIn;

            // Add the current center pixel to sumOut
            auto middleIndex = (queueIndex + radius) % queue.size();
            sumOut += queue[middleIndex];

            // *remove* the new center pixel from sumIn
            sumIn -= queue[middleIndex];
        }
    }

    // vertical pass, loop around each column
    // pretty much identical to the horizontal pass
    // except the pixel access is vertical instead of horizontal
    // hey, it's a naive implementation! prob better for perf this way too
    for (auto x = 0u; x < w; ++x)
    {
        // clear everything
        stackSum = sumIn = sumOut = queueIndex = 0;

        // Pre-fill the left half of the queue with the topmost pixel value
        auto topMostPixel = buffer + pixelStride * x; // data.getLinePointer(0) + (unsigned int)data.pixelStride * x;
        for (auto i = 0u; i <= radius; ++i)
        {
            queue[i] = topMostPixel[0];

            // these init left side + middle of the stack
            sumOut += queue[i];
            stackSum += queue[i] * (i + 1);
        }

        // Fill the right half of the queue (excluding center!) with actual pixel values
        // zero is the topmost/center pixel here (it was added to the sum (radius + 1) times above)
        for (auto i = 1u; i <= radius; ++i)
        {
            if (i <= h - 1)
            {
                auto pixel = buffer + pixelStride * (x + i * w); //  data.getLinePointer(i) + (unsigned int)data.pixelStride * x;
                queue[radius + i] = pixel[0];
            }
            // edge case where queue is bigger than image height!
            // for example where width = 1
            else
            {
                auto pixel = buffer + pixelStride * (x + (h - 1) * w); //data.getLinePointer((h - 1)) + (unsigned int)data.pixelStride * x;
                queue[radius + i] = pixel[0];
            }

            sumIn += queue[radius + i];
            stackSum += queue[radius + i] * (radius + 1 - i);
        }

        for (auto y = 0u; y < h; ++y)
        {
            // calculate the blurred value from the stack
            auto blurredValue = stackSum / sizeOfStack;
//            assert(blurredValue <= 1.0f);
            auto blurredPixel = buffer + pixelStride * (w * y + x); // data.getLinePointer(static_cast<int>(y)) + (unsigned int)data.pixelStride * x;
            blurredPixel[0] = /*static_cast<unsigned char>*/(blurredValue);

            // remove outgoing sum from the stack
            stackSum -= sumOut;

            // start crafting the next sumOut by removing the leftmost value from sumOut
            sumOut -= queue[queueIndex];

            // Replace the leftmost value with the incoming value
            if (y + radius + 1 < h)
                queue[queueIndex] = buffer[pixelStride * (w * (y + radius + 1) + x)]; // *data.getPixelPointer(static_cast<int> (x), static_cast<int> (y + radius + 1));
            else
                queue[queueIndex] = buffer[pixelStride * (w * (h - 1) + x)]; // *data.getPixelPointer(static_cast<int> (x), static_cast<int> (h - 1));

            // Add the incoming value to the sumIn
            sumIn += queue[queueIndex];

            // Advance the queue index by 1 position
            if (++queueIndex == queue.size()) queueIndex = 0;

            // Put into place the next incoming sums
            stackSum += sumIn;

            // Before we move the queue/kernel, add the current center pixel to sumOut
            auto middleIndex = (queueIndex + radius) % queue.size();
            sumOut += queue[middleIndex];

            // *remove* the new center pixel (only after the window shifts)
            sumIn -= queue[middleIndex];
        }
    }
}

const unsigned short stackblur_mul[255] = { 512, 512, 456, 512, 328, 456, 335, 512, 405, 328, 271, 456, 388, 335, 292, 512, 454, 405, 364, 328, 298, 271, 496, 456, 420, 388, 360, 335, 312, 292, 273, 512, 482, 454, 428, 405, 383, 364, 345, 328, 312, 298, 284, 271, 259, 496, 475, 456, 437, 420, 404, 388, 374, 360, 347, 335, 323, 312, 302, 292, 282, 273, 265, 512, 497, 482, 468, 454, 441, 428, 417, 405, 394, 383, 373, 364, 354, 345, 337, 328, 320, 312, 305, 298, 291, 284, 278, 271, 265, 259, 507, 496, 485, 475, 465, 456, 446, 437, 428, 420, 412, 404, 396, 388, 381, 374, 367, 360, 354, 347, 341, 335, 329, 323, 318, 312, 307, 302, 297, 292, 287, 282, 278, 273, 269, 265, 261, 512, 505, 497, 489, 482, 475, 468, 461, 454, 447, 441, 435, 428, 422, 417, 411, 405, 399, 394, 389, 383, 378, 373, 368, 364, 359, 354, 350, 345, 341, 337, 332, 328, 324, 320, 316, 312, 309, 305, 301, 298, 294, 291, 287, 284, 281, 278, 274, 271, 268, 265, 262, 259, 257, 507, 501, 496, 491, 485, 480, 475, 470, 465, 460, 456, 451, 446, 442, 437, 433, 428, 424, 420, 416, 412, 408, 404, 400, 396, 392, 388, 385, 381, 377, 374, 370, 367, 363, 360, 357, 354, 350, 347, 344, 341, 338, 335, 332, 329, 326, 323, 320, 318, 315, 312, 310, 307, 304, 302, 299, 297, 294, 292, 289, 287, 285, 282, 280, 278, 275, 273, 271, 269, 267, 265, 263, 261, 259 };
const unsigned char stackblur_shr[255] = { 9, 11, 12, 13, 13, 14, 14, 15, 15, 15, 15, 16, 16, 16, 16, 17, 17, 17, 17, 17, 17, 17, 18, 18, 18, 18, 18, 18, 18, 18, 18, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24 };

inline uint8_t* getLinePointer(int y, uint8_t* buffer, int w)
{
    return buffer + (w * y);
}
inline uint8_t* getPixelPointer(int x, int y, uint8_t* buffer, int w)
{
    return buffer + (w * y + x);
}

// ONLY USING THIS ONE!!!
static void ginSingleChannel(uint8_t* buffer, unsigned int w, unsigned int h, unsigned int radius)
{
    //const unsigned int w = (unsigned int)img.getWidth();
    //const unsigned int h = (unsigned int)img.getHeight();

    //juce::Image::BitmapData data(img, juce::Image::BitmapData::readWrite);
    const auto lineStride = w;

    radius = std::clamp(radius, 1u, 254u);

    unsigned char stack[(254 * 2 + 1) * 1];

    unsigned int x, y, xp, yp, i, sp, stack_start;

    unsigned char* stack_ptr = nullptr;
    unsigned char* src_ptr = nullptr;
    unsigned char* dst_ptr = nullptr;

    unsigned long sum, sum_in, sum_out;

    unsigned int wm = w - 1;
    unsigned int hm = h - 1;
    unsigned int w1 = /*(unsigned int)data.*/lineStride;
    unsigned int div = (unsigned int)(radius * 2) + 1;
    unsigned int mul_sum = stackblur_mul[radius];
    unsigned char shr_sum = stackblur_shr[radius];

    for (y = 0; y < h; ++y)
    {
        sum = sum_in = sum_out = 0;

        src_ptr = getLinePointer(y, buffer, w); // data.getLinePointer(int(y));

        for (i = 0; i <= radius; ++i)
        {
            stack_ptr = &stack[i];
            stack_ptr[0] = src_ptr[0];
            sum += src_ptr[0] * (i + 1);
            sum_out += src_ptr[0];
        }

        for (i = 1; i <= radius; ++i)
        {
            if (i <= wm)
                src_ptr += 1;

            stack_ptr = &stack[1 * (i + radius)];
            stack_ptr[0] = src_ptr[0];
            sum += src_ptr[0] * (radius + 1 - i);
            sum_in += src_ptr[0];
        }

        sp = radius;
        xp = radius;
        if (xp > wm)
            xp = wm;

        src_ptr = getPixelPointer(xp, y, buffer, w); // data.getLinePointer(int(y)) + (unsigned int)data.pixelStride * xp;
        dst_ptr = getLinePointer(y, buffer, w); // data.getLinePointer(int(y));

        for (x = 0; x < w; ++x)
        {
            dst_ptr[0] = (unsigned char)((sum * mul_sum) >> shr_sum);
            dst_ptr += 1;

            sum -= sum_out;

            stack_start = sp + div - radius;

            if (stack_start >= div)
                stack_start -= div;

            stack_ptr = &stack[1 * stack_start];

            sum_out -= stack_ptr[0];

            if (xp < wm)
            {
                src_ptr += 1;
                ++xp;
            }

            stack_ptr[0] = src_ptr[0];

            sum_in += src_ptr[0];
            sum += sum_in;

            ++sp;
            if (sp >= div)
                sp = 0;

            stack_ptr = &stack[sp * 1];

            sum_out += stack_ptr[0];
            sum_in -= stack_ptr[0];
        }
    }

    for (x = 0; x < w; ++x)
    {
        sum = sum_in = sum_out = 0;

        src_ptr = getPixelPointer(x, 0, buffer, w); // data.getLinePointer(0) + (unsigned int)data.pixelStride * x;

        for (i = 0; i <= radius; ++i)
        {
            stack_ptr = &stack[i * 1];
            stack_ptr[0] = src_ptr[0];
            sum += src_ptr[0] * (i + 1);
            sum_out += src_ptr[0];
        }

        for (i = 1; i <= radius; ++i)
        {
            if (i <= hm)
                src_ptr += w1;

            stack_ptr = &stack[1 * (i + radius)];
            stack_ptr[0] = src_ptr[0];
            sum += src_ptr[0] * (radius + 1 - i);
            sum_in += src_ptr[0];
        }

        sp = radius;
        yp = radius;
        if (yp > hm)
            yp = hm;

        src_ptr = getPixelPointer(x, yp, buffer, w); // data.getLinePointer(int(yp)) + (unsigned int)data.pixelStride * x;
        dst_ptr = getPixelPointer(x, 0, buffer, w); // data.getLinePointer(0) + (unsigned int)data.pixelStride * x;

        for (y = 0; y < h; ++y)
        {
            dst_ptr[0] = (unsigned char)((sum * mul_sum) >> shr_sum);
            dst_ptr += w1;

            sum -= sum_out;

            stack_start = sp + div - radius;
            if (stack_start >= div)
                stack_start -= div;

            stack_ptr = &stack[1 * stack_start];

            sum_out -= stack_ptr[0];

            if (yp < hm)
            {
                src_ptr += w1;
                ++yp;
            }

            stack_ptr[0] = src_ptr[0];

            sum_in += src_ptr[0];
            sum += sum_in;

            ++sp;
            if (sp >= div)
                sp = 0;

            stack_ptr = &stack[sp * 1];

            sum_out += stack_ptr[0];
            sum_in -= stack_ptr[0];
        }
    }
}

inline half* getLinePointer(int y, half* buffer, int w)
{
    constexpr int pixelStride = 4;
    return buffer + pixelStride * (w * y);
}
inline half* getPixelPointer(int x, int y, half* buffer, int w)
{
    constexpr int pixelStride = 4;
    return buffer + pixelStride * (w * y + x);
}

static void ginARGB(half* buffer, unsigned int w, unsigned int h, unsigned int radius)
{
    //const unsigned int w = (unsigned int)img.getWidth();
    //const unsigned int h = (unsigned int)img.getHeight();

    //juce::Image::BitmapData data(img, juce::Image::BitmapData::readWrite);

    radius = std::clamp(radius, 1u, 254u);

   float stack[(254 * 2 + 1) * 4];

   unsigned int x, y, xp, yp, i, sp, stack_start;

   float* stack_ptr = nullptr;
   half* src_ptr = nullptr;
   half* dst_ptr = nullptr;

   float sum_r, sum_g, sum_b, sum_a, sum_in_r, sum_in_g, sum_in_b, sum_in_a,
        sum_out_r, sum_out_g, sum_out_b, sum_out_a;

    unsigned int wm = w - 1;
    unsigned int hm = h - 1;
    unsigned int w4 = (unsigned int) w * 4; // data.lineStride;
    const float div = (radius * 2) + 1;
    const float mul_sum = stackblur_mul[radius] / (float)(1 << stackblur_shr[radius]);
//  unsigned int shr_sum = stackblur_shr[radius];

    for (y = 0; y < h; ++y)
    {
        sum_r = sum_g = sum_b = sum_a =
            sum_in_r = sum_in_g = sum_in_b = sum_in_a =
            sum_out_r = sum_out_g = sum_out_b = sum_out_a = 0;

        src_ptr = getLinePointer(y, buffer, w); // data.getLinePointer(int(y));

        for (i = 0; i <= radius; ++i)
        {
            stack_ptr = &stack[4 * i];
            stack_ptr[0] = src_ptr[0];
            stack_ptr[1] = src_ptr[1];
            stack_ptr[2] = src_ptr[2];
            stack_ptr[3] = src_ptr[3];
            sum_r += src_ptr[0] * (i + 1);
            sum_g += src_ptr[1] * (i + 1);
            sum_b += src_ptr[2] * (i + 1);
            sum_a += src_ptr[3] * (i + 1);
            sum_out_r += src_ptr[0];
            sum_out_g += src_ptr[1];
            sum_out_b += src_ptr[2];
            sum_out_a += src_ptr[3];
        }

        for (i = 1; i <= radius; ++i)
        {
            if (i <= wm)
                src_ptr += 4;

            stack_ptr = &stack[4 * (i + radius)];
            stack_ptr[0] = src_ptr[0];
            stack_ptr[1] = src_ptr[1];
            stack_ptr[2] = src_ptr[2];
            stack_ptr[3] = src_ptr[3];
            sum_r += src_ptr[0] * (radius + 1 - i);
            sum_g += src_ptr[1] * (radius + 1 - i);
            sum_b += src_ptr[2] * (radius + 1 - i);
            sum_a += src_ptr[3] * (radius + 1 - i);
            sum_in_r += src_ptr[0];
            sum_in_g += src_ptr[1];
            sum_in_b += src_ptr[2];
            sum_in_a += src_ptr[3];
        }

        sp = radius;
        xp = radius;
        if (xp > wm)
            xp = wm;

        src_ptr = getPixelPointer(xp, y, buffer, w); // data.getLinePointer(int(y)) + (unsigned int)data.pixelStride * xp;
        dst_ptr = getLinePointer(y, buffer, w); // data.getLinePointer(int(y));

        for (x = 0; x < w; ++x)
        {
            dst_ptr[0] = sum_r * mul_sum; // (unsigned char)((sum_r * mul_sum) >> shr_sum);
            dst_ptr[1] = sum_g * mul_sum; // (unsigned char)((sum_g * mul_sum) >> shr_sum);
            dst_ptr[2] = sum_b * mul_sum; // (unsigned char)((sum_b * mul_sum) >> shr_sum);
            dst_ptr[3] = sum_a * mul_sum; // (unsigned char)((sum_a * mul_sum) >> shr_sum);
            dst_ptr += 4;

            sum_r -= sum_out_r;
            sum_g -= sum_out_g;
            sum_b -= sum_out_b;
            sum_a -= sum_out_a;

            stack_start = sp + div - radius;

            if (stack_start >= div)
                stack_start -= div;

            stack_ptr = &stack[4 * stack_start];

            sum_out_r -= stack_ptr[0];
            sum_out_g -= stack_ptr[1];
            sum_out_b -= stack_ptr[2];
            sum_out_a -= stack_ptr[3];

            if (xp < wm)
            {
                src_ptr += 4;
                ++xp;
            }

            stack_ptr[0] = src_ptr[0];
            stack_ptr[1] = src_ptr[1];
            stack_ptr[2] = src_ptr[2];
            stack_ptr[3] = src_ptr[3];

            sum_in_r += src_ptr[0];
            sum_in_g += src_ptr[1];
            sum_in_b += src_ptr[2];
            sum_in_a += src_ptr[3];
            sum_r += sum_in_r;
            sum_g += sum_in_g;
            sum_b += sum_in_b;
            sum_a += sum_in_a;

            ++sp;
            if (sp >= div)
                sp = 0;

            stack_ptr = &stack[sp * 4];

            sum_out_r += stack_ptr[0];
            sum_out_g += stack_ptr[1];
            sum_out_b += stack_ptr[2];
            sum_out_a += stack_ptr[3];
            sum_in_r -= stack_ptr[0];
            sum_in_g -= stack_ptr[1];
            sum_in_b -= stack_ptr[2];
            sum_in_a -= stack_ptr[3];
        }
    }

    for (x = 0; x < w; ++x)
    {
        sum_r = sum_g = sum_b = sum_a =
            sum_in_r = sum_in_g = sum_in_b = sum_in_a =
            sum_out_r = sum_out_g = sum_out_b = sum_out_a = 0;

        src_ptr = getPixelPointer(x, 0, buffer, w); // data.getLinePointer(0) + (unsigned int)data.pixelStride * x;

        for (i = 0; i <= radius; ++i)
        {
            stack_ptr = &stack[i * 4];
            stack_ptr[0] = src_ptr[0];
            stack_ptr[1] = src_ptr[1];
            stack_ptr[2] = src_ptr[2];
            stack_ptr[3] = src_ptr[3];
            sum_r += src_ptr[0] * (i + 1);
            sum_g += src_ptr[1] * (i + 1);
            sum_b += src_ptr[2] * (i + 1);
            sum_a += src_ptr[3] * (i + 1);
            sum_out_r += src_ptr[0];
            sum_out_g += src_ptr[1];
            sum_out_b += src_ptr[2];
            sum_out_a += src_ptr[3];
        }

        for (i = 1; i <= radius; ++i)
        {
            if (i <= hm)
                src_ptr += w4;

            stack_ptr = &stack[4 * (i + radius)];
            stack_ptr[0] = src_ptr[0];
            stack_ptr[1] = src_ptr[1];
            stack_ptr[2] = src_ptr[2];
            stack_ptr[3] = src_ptr[3];
            sum_r += src_ptr[0] * (radius + 1 - i);
            sum_g += src_ptr[1] * (radius + 1 - i);
            sum_b += src_ptr[2] * (radius + 1 - i);
            sum_a += src_ptr[3] * (radius + 1 - i);
            sum_in_r += src_ptr[0];
            sum_in_g += src_ptr[1];
            sum_in_b += src_ptr[2];
            sum_in_a += src_ptr[3];
        }

        sp = radius;
        yp = radius;
        if (yp > hm)
            yp = hm;

        src_ptr = getPixelPointer(x, yp, buffer, w); // data.getLinePointer(int(yp)) + (unsigned int)data.pixelStride * x;
        dst_ptr = getPixelPointer(x,  0, buffer, w); // data.getLinePointer(0) + (unsigned int)data.pixelStride * x;

        for (y = 0; y < h; ++y)
        {
            dst_ptr[0] = sum_r * mul_sum; // (unsigned char)((sum_r * mul_sum) >> shr_sum);
            dst_ptr[1] = sum_g * mul_sum; // (unsigned char)((sum_g * mul_sum) >> shr_sum);
            dst_ptr[2] = sum_b * mul_sum; // (unsigned char)((sum_b * mul_sum) >> shr_sum);
            dst_ptr[3] = sum_a * mul_sum; // (unsigned char)((sum_a * mul_sum) >> shr_sum);
            dst_ptr += w4;

            sum_r -= sum_out_r;
            sum_g -= sum_out_g;
            sum_b -= sum_out_b;
            sum_a -= sum_out_a;

            stack_start = sp + div - radius;
            if (stack_start >= div)
                stack_start -= div;

            stack_ptr = &stack[4 * stack_start];

            sum_out_r -= stack_ptr[0];
            sum_out_g -= stack_ptr[1];
            sum_out_b -= stack_ptr[2];
            sum_out_a -= stack_ptr[3];

            if (yp < hm)
            {
                src_ptr += w4;
                ++yp;
            }

            stack_ptr[0] = src_ptr[0];
            stack_ptr[1] = src_ptr[1];
            stack_ptr[2] = src_ptr[2];
            stack_ptr[3] = src_ptr[3];

            sum_in_r += src_ptr[0];
            sum_in_g += src_ptr[1];
            sum_in_b += src_ptr[2];
            sum_in_a += src_ptr[3];
            sum_r += sum_in_r;
            sum_g += sum_in_g;
            sum_b += sum_in_b;
            sum_a += sum_in_a;

            ++sp;
            if (sp >= div)
                sp = 0;

            stack_ptr = &stack[sp * 4];

            sum_out_r += stack_ptr[0];
            sum_out_g += stack_ptr[1];
            sum_out_b += stack_ptr[2];
            sum_out_a += stack_ptr[3];
            sum_in_r -= stack_ptr[0];
            sum_in_g -= stack_ptr[1];
            sum_in_b -= stack_ptr[2];
            sum_in_a -= stack_ptr[3];
        }
    }
}

inline uint8_t fast8bitScale(uint8_t a, uint8_t b)
{
    const int t = (int)a * (int)b;
    return (uint8_t)((t + 1 + (t >> 8)) >> 8); // fast way to divide by 255
}

class GmpiUiTest : public PluginEditor
{
    Bitmap buffer;
    Bitmap buffer2;

public:
	ReturnCode render(gmpi::drawing::api::IDeviceContext* drawingContext) override
	{
		Graphics g(drawingContext);

		if (!buffer)
		{
			// draw on a bitmap mask
			Size mysize(100, 100);
            SizeU mysize2(100, 100);
            {
                // TODO: is this meant to create it in DIPs or hardware pixels? why is it float?
				auto dc = g.createCompatibleRenderTarget(mysize, (int32_t)BitmapRenderTargetFlags::Mask | (int32_t)BitmapRenderTargetFlags::CpuReadable);
				dc.beginDraw();


				// your drawing here
                auto brush = dc.createSolidColorBrush(Colors::White);
				dc.drawCircle({ 50, 50 }, 40, brush, 5.0f);

				dc.drawLine({ 0, 100 }, { 100, 90 }, brush, 5.0f);

				dc.endDraw();
				buffer = dc.getBitmap();
			}

			// modify the buffer
			if(true)
			{
				auto data = buffer.lockPixels(BitmapLockFlags::ReadWrite);

				{
					auto imageSize = buffer.getSize();
					constexpr int pixelSize = 8; // 8 bytes per pixel for half-float
					auto stride = data.getBytesPerRow();
					auto format = data.getPixelFormat();
					const int totalPixels = (int)imageSize.height * stride / pixelSize;

					const int pixelSizeTest = stride / imageSize.width; // 8 for half-float RGB, 4 for 8-bit sRGB, 1 for alpha mask

#if 0
					// un-premultiply alpha
                    {
					    auto pixel = (half*) data.getAddress();

						for (int i = 0; i < totalPixels; ++i)
						{
							const float alpha = pixel[3];
							if (alpha != 1.0f && alpha != 0.0f)
							{
								const float overAlphaNorm = 1.0f / alpha;
								for (int j = 0; j < 3; ++j)
								{
									const float p = pixel[j];
									if (p != 0.0f)
									{
                                        pixel[j] = p * overAlphaNorm;
									}
								}
							}
							pixel += 4;
						}
                    }
#endif
					// modify pixels here
#if 0
                    {
                        auto pixel = (half*)data.getAddress();
                        ginARGB(pixel, imageSize.width, imageSize.height, 5);
                    }
#else
                    {
                        // create a blurred mask of the image.
                        auto pixel = data.getAddress();
                        ginSingleChannel(pixel, imageSize.width, imageSize.height, 5);
                    }
#endif


#if 0
                    // re-premultiply alpha
                    {
                        auto pixel = (half*)data.getAddress();

                        for (int i = 0; i < totalPixels; ++i)
                        {
                            const float alpha = pixel[3];
                            if (alpha == 0.0f)
                            {
                                pixel[0] = pixel[1] = pixel[2] = 0.0f;
                            }
                            else
                            {
                                for (int j = 0; j < 3; ++j)
                                {
                                    const float p = pixel[j];
                                    pixel[j] = p * alpha;
                                }
                            }
                            pixel += 4;
                        }
                    }
#endif
                    // create bitmap
					buffer2 = g.getFactory().createImage(mysize2, (int32_t)gmpi::drawing::BitmapRenderTargetFlags::EightBitPixels);
                    {
                        auto destdata = buffer2.lockPixels(BitmapLockFlags::Write);
                        auto imageSize = buffer2.getSize();
                        constexpr int pixelSize = 4; // 8 bytes per pixel for half-float, 4 for 8-bit
                        auto stride = destdata.getBytesPerRow();
                        auto format = destdata.getPixelFormat();
                        const int totalPixels = (int)imageSize.height * stride / pixelSize;

                        const int pixelSizeTest = stride / imageSize.width; // 8 for half-float RGB, 4 for 8-bit sRGB, 1 for alpha mask

                        auto pixelsrc = data.getAddress();
//                        auto pixeldest = (half*)destdata.getAddress();
                        auto pixeldest = destdata.getAddress();

						uint8_t tint8[4] = { 0xff, 0xd4, 0xc1, 0xff }; // xff7777ff
                        Color tint = colorFromArgb(tint8[0], tint8[1], tint8[2]);// { Colors::BlueViolet };
						float tintf[4] = { tint.r, tint.g, tint.b, tint.a };

                        constexpr float inv255 = 1.0f / 255.0f;

                        for (int i = 0; i < totalPixels; ++i)
                        {
                            const auto alpha = *pixelsrc;
                            if (alpha == 0)
                            {
                                pixeldest[0] = pixeldest[1] = pixeldest[2] = pixeldest[3] = 0.0f;
                            }
                            else
                            {
                                //for (int j = 0; j < 3; ++j)
                                //{
                                //    const float p = 1.0f;
                                //    pixeldest[j] = p * alpha;
                                //}
								// _RPTN(0, "%f\n", alpha / 255.0f);
#if 1 // correct premultiply. much brighter.
                                const float AlphaNorm = alpha * inv255;
                                for (int j = 0; j < 3; ++j)
                                {
                                    // To linear
                                    auto cf = tintf[j]; // gmpi::drawing::SRGBPixelToLinear(static_cast<unsigned char>(tint8[i]));

                                    // pre-multiply in linear space.
                                    cf *= AlphaNorm;

                                    // back to SRGB
                                    pixeldest[j] = gmpi::drawing::linearPixelToSRGB(cf);
                                }
#else // naive premultply
                                pixeldest[0] = fast8bitScale(alpha, tint8[0]); // alpha* tint.r* inv255;
								pixeldest[1] = fast8bitScale(alpha, tint8[1]); // alpha * tint.g * inv255;
								pixeldest[2] = fast8bitScale(alpha, tint8[2]); // alpha * tint.b * inv255;
#endif
								pixeldest[3] = alpha; //alpha * inv255;
                            }

                            pixelsrc++;
                            pixeldest += 4;
                        }
                    }
/*
					for (float y = 0; y < imageSize.height; y++)
					{
						for (float x = 0; x < imageSize.width; x++)
						{
							// pixel to Color.
							gmpi::drawing::Color color{ pixel[0], pixel[1], pixel[2], pixel[3] };

							if (pixel[0] > 0.0f)
							{
								color.r = x / imageSize.width;
								color.g = 1.0f - pixel[0];
								color.b = y / imageSize.width;
								color.a = 1.0f - pixel[2];
							}

							// Color to pixel
							pixel[0] = color.r;
							pixel[1] = color;
							pixel[2] = color;
							pixel[3] = color;

							pixel += 4;
						}
					}
*/
				}
			}
		}

		ClipDrawingToBounds _(g, bounds);

		g.clear(Colors::Black);

//		drawKnob(g, bounds, pinGain.value);
		
		g.drawBitmap(buffer2, Point(0, 0), bounds);

        // your drawing here, again
        auto brush = g.createSolidColorBrush(Colors::White);
        g.drawCircle({ 50, 50 }, 40, brush, 5.0f);

		return ReturnCode::Ok;
	}
};

// Describe the plugin and register it with the framework.
namespace
{
auto r = Register<GmpiUiTest>::withXml(R"XML(
<?xml version="1.0" encoding="utf-8" ?>

<PluginList>
  <Plugin id="SE: GmpiUiTest" name="GMPI-UI Test" category="GMPI/SDK Examples" vendor="Jeff McClintock">
    <GUI graphicsApi="GmpiGui"/>
  </Plugin>
</PluginList>
)XML");
}