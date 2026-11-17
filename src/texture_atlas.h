#ifndef TEXTURE_ATLAS_H
#define TEXTURE_ATLAS_H

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <iostream>
#include <limits>
#include <vector>

#include "vector_util.h"
#include <SFML/Graphics.hpp>

// RectPacker maintains two offsets into an overall bin size:
//  - A line y-offset, representing the current horizontal line on which to place new rectangles
//      horizontally along (the line corresponding to the rectangle tops)
//  - An x-offset into the line, representing the x-coordinate to align the next rectangle left-side
// Furthermore it maintains the maximum texture height encountered along the current line.
// The RectPacker places rectangles left to right along this line until it cannot fit the next
//  rectangle. Once this happens, the line is shifted down to meet the bottom of the tallest texture
//  placed on the line, and the x-offset is reset. This continues until the next rectangle
//  packed cannot be fit on the line within the bin space.
class RectPacker {
public:
    RectPacker(const sf::Vector2f& size) : mBinSize{size} {}

    // pack returns { succeeded, rect id }. If not succeeded, id is irrelevant (but ideally should
    // be set to the maximum possible value).
    std::pair<bool, size_t> pack(const sf::Vector2f& rectSize) {
        const size_t invalidId = std::numeric_limits<size_t>::max();

        if (rectSize.x > mBinSize.x) {
            return {false, invalidId};
        }

        // doesnt fit in this row so start new row below tallest rect
        if (mNextX + rectSize.x > mBinSize.x) {
            mRowY += mTallestInRow;
            mNextX = 0.0f;
            mTallestInRow = 0.0f;
        }

        if (mRowY + rectSize.y > mBinSize.y) {
            return {false, invalidId};
        }

        mPackedRects.push_back(sf::FloatRect({mNextX, mRowY}, rectSize));
        mNextX += rectSize.x;
        mTallestInRow = std::max(mTallestInRow, rectSize.y);
        return {true, mPackedRects.size() - 1};
    }

    // Returns the coordinates and size of a previously returned rectangle id.
    // Note that in this coordinate system, y points downwards from the origin at the top-left.
    sf::FloatRect getBounds(size_t rectID) const {
        assert(rectID < mPackedRects.size());
        return mPackedRects[rectID];
    }

private:
    sf::Vector2f mBinSize;
    float mNextX = 0.0f;
    float mRowY = 0.0f;
    float mTallestInRow = 0.0f;
    std::vector<sf::FloatRect> mPackedRects;
};

// Returns a new image which pages the given img with *pixels* pixels on each side.
// The colour of new border pixels are taken by sampling the closest valid pixel in the original
// image.
//
// NOTE: a production implementation might also support other edge wrapping behaviors, like cycling
// around to the other side of the texture. See https://learnopengl.com/Getting-started/Textures
// Our implementation corresponds to GL_CLAMP_TO_EDGE
inline sf::Image padImage(const sf::Image& img, unsigned int pixels) {
    const sf::Vector2u origSize = img.getSize();
    sf::Image paddedImg(sf::Vector2u(origSize.x + 2 * pixels, origSize.y + 2 * pixels));

    for (unsigned int newY = 0; newY < paddedImg.getSize().y; ++newY) {
        for (unsigned int newX = 0; newX < paddedImg.getSize().x; ++newX) {
            // clamping gives nearest pixel inside origin image
            unsigned int closestX;
            if (newX < pixels) {
                closestX = 0;
            } else if (newX - pixels >= origSize.x) {
                closestX = origSize.x - 1;
            } else {
                closestX = newX - pixels;
            }

            unsigned int closestY;
            if (newY < pixels) {
                closestY = 0;
            } else if (newY - pixels >= origSize.y) {
                closestY = origSize.y - 1;
            } else {
                closestY = newY - pixels;
            }

            paddedImg.setPixel({newX, newY}, img.getPixel({closestX, closestY}));
        }
    }
    return paddedImg;
}

using TextureID = size_t;

class TextureAtlas {
public:
    TextureAtlas(sf::Vector2u binSize, size_t padding)
        : mTexture(binSize), mPacker(sf::Vector2f(mTexture.getSize())), mPadding{padding} {
        // new texture memory not guaranteed to be empty so clear it to transparent
        mTexture.update(sf::Image(binSize, sf::Color::Transparent));
    }

    TextureID registerTexture(const std::filesystem::path& filepath) {
        const sf::Image loadedImg(filepath);
        const sf::Image paddedImg = padImage(loadedImg, static_cast<unsigned int>(mPadding));

        auto packResult = mPacker.pack(sf::Vector2f(paddedImg.getSize()));
        if (!packResult.first) {
            std::cerr << "Not enough space left in texture atlas for " << filepath << '\n';
            return packResult.second;
        }

        mTexture.update(paddedImg, sf::Vector2u(mPacker.getBounds(packResult.second).position));
        return packResult.second;
    }

    sf::FloatRect getBoundsUV(TextureID id) const {
        const sf::FloatRect boundsInPixels = getUnpaddedBounds(id);
        const sf::Vector2f atlasSize(mTexture.getSize());

        const float uLeft = boundsInPixels.position.x / atlasSize.x;
        // packer has y going down from top, but UV has v going up from bottom
        const float bottomY = boundsInPixels.position.y + boundsInPixels.size.y;
        const float vBottom = 1.0f - bottomY / atlasSize.y;
        const float uvWidth = boundsInPixels.size.x / atlasSize.x;
        const float uvHeight = boundsInPixels.size.y / atlasSize.y;
        return sf::FloatRect({uLeft, vBottom}, {uvWidth, uvHeight});
    }

    sf::Vector2u getSizePixels(TextureID id) const {
        return sf::Vector2u(getUnpaddedBounds(id).size);
    }

    const sf::Texture& getTexture() const { return mTexture; }

private:
    sf::FloatRect getUnpaddedBounds(TextureID id) const {
        const sf::FloatRect paddedBounds = mPacker.getBounds(id);
        const float padAmount = static_cast<float>(mPadding);
        return sf::FloatRect(paddedBounds.position + sf::Vector2f(padAmount, padAmount),
                             paddedBounds.size - sf::Vector2f(2.0f * padAmount, 2.0f * padAmount));
    }

    sf::Texture mTexture;
    RectPacker mPacker;
    size_t mPadding;
};

#endif  // TEXTURE_ATLAS_H
