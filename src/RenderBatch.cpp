#include "stdafx.h"
#include "RenderBatch.h"

#include "VectorMath.h"

#include "Constants.h"

xr::RenderBatch::RenderBatch() :
	defaultTexture(1, 1, GL_RGBA, new unsigned char[4]{255, 255, 255, 255}),
	currentTextureRegion(0, 0, 1, 1)
{
	this->clearTexture();
}

void xr::RenderBatch::clear()
{
	this->textureBatches.clear();

	this->meshBuffer.vertices.clear();
	this->meshBuffer.indices.clear();
	
	this->currentTransformation = glm::mat4();
	this->fillColor = { 1.0, 1.0, 1.0, 1.0 };

	this->clearTexture();
}

void xr::RenderBatch::setFillColor(glm::vec4 color)
{
	this->fillColor = color;
}

void xr::RenderBatch::setCamera(const Camera & camera)
{
	this->setCamera(camera.getTransform());
}

void xr::RenderBatch::setCamera(const glm::mat4 & cameraMatrix)
{
	this->currentTransformation = cameraMatrix;

	this->currentTextureBatch->transBatches.emplace_back(currentTransformation);
	this->currentIndexRange = &this->currentTextureBatch->transBatches.back().indexRange;
	this->currentIndexRange->lower = this->currentIndexRange->upper = this->meshBuffer.indices.size();
}

void xr::RenderBatch::setTexture(const Texture & texture, const Rectangle<float>& region)
{
	this->currentTextureRegion = region;

	TextureBatch& textureBatch = this->textureBatches[texture];

	if (this->currentTextureBatch == &textureBatch) {
		return;
	}

	this->currentTextureBatch = &textureBatch;

	// Add a new Transformation batch if this is a new texture or
	// the matrix has changed since last time this texture was used
	if (textureBatch.transBatches.size() == 0 ||
		textureBatch.transBatches.back().transformation != currentTransformation) {
		this->setCamera(currentTransformation);
	}

	this->currentIndexRange = &this->currentTextureBatch->transBatches.back().indexRange;
}

void xr::RenderBatch::setTexture(const TextureRegion & region)
{
	this->setTexture(region.getTexture(), region.getRegion());
}

void xr::RenderBatch::clearTexture()
{
	this->setTexture(this->defaultTexture);
}


void xr::RenderBatch::fillRect(float x, float y, float w, float h)
{
	// Get the index of the last vertex and start indexing from there
	int startIndex = this->meshBuffer.vertices.size();

	// Add indices to all vertices
	this->meshBuffer.indices.emplace_back(0 + startIndex);
	this->meshBuffer.indices.emplace_back(1 + startIndex);
	this->meshBuffer.indices.emplace_back(2 + startIndex);
	this->meshBuffer.indices.emplace_back(2 + startIndex);
	this->meshBuffer.indices.emplace_back(3 + startIndex);
	this->meshBuffer.indices.emplace_back(0 + startIndex);

	// Increase index range
	this->currentIndexRange->upper += 6;
	
	// Provide alias for 'currentTextureRegion'
	auto& r = this->currentTextureRegion;

	float z = 0;

	// Add vertices
	this->meshBuffer.vertices.emplace_back(glm::vec3{ x, y, z }, glm::vec2{ r.x, r.y + r.height }, this->fillColor);
	this->meshBuffer.vertices.emplace_back(glm::vec3{ x, y + h, z }, glm::vec2{ r.x, r.y }, this->fillColor);
	this->meshBuffer.vertices.emplace_back(glm::vec3{ x + w, y + h, z }, glm::vec2{ r.x + r.width, r.y }, this->fillColor);
	this->meshBuffer.vertices.emplace_back(glm::vec3{ x + w, y, z }, glm::vec2{ r.x + r.width, r.y + r.height }, this->fillColor);
}



/*
////////////////////
ALGORITHM:
////////////////////

//// Determine inside
1. Calculate the sum of all the corner's angles, counterclockwise and clockwise
2. The side with the smaller angle sum is the inside

//// Construct triangles
3. Calculate the inner angle of the next corner
4. If the angle is greater than 180 degrees, set that as the new centroid
6. Otherwise construct triangle with the next 2 following points
5. Repeat step 3-5 until all triangles are constructed
*/
void xr::RenderBatch::fillPolygon(const std::vector<glm::vec2>& points)
{
	int pointCount = points.size();

	if (pointCount < 3) {
		return;
	}

#define IND(index) ((index + pointCount) % pointCount)

	auto getPoint = [&](int index) {
		return points[IND(index)];
	};

	// List of all corner's angles
	std::vector<float> angles(pointCount);


	// Determine inside

	// Calculate corner angles
	for (int i = 0; i < pointCount; i++) {
		glm::vec2 o = getPoint(i);
		glm::vec2 a = glm::normalize(getPoint(i - 1) - o);
		glm::vec2 b = glm::normalize(getPoint(i + 1) - o);

		float angle = angleBetween(a, b) + float(PI);

		angles[i] = angle;
	}


	// Sum all angles
	float angleSum = 0;
	for (int i = 0; i < pointCount; i++)
	{
		angleSum += angles[i];
	}

	// The total should be less than pointCount * PI if it's clockwise
	bool clockwise = angleSum < pointCount * float(PI);


	// Recalculate angles
	if (!clockwise) {
		for (int i = 0; i < pointCount; i++)
		{
			angles[i] = 2 * float(PI) - angles[i];
		}
	}

	// Construct triangles

	// Add vertices
	for (int i = 0; i < pointCount; i++)
	{
		this->meshBuffer.vertices.emplace_back(glm::vec3(getPoint(i), 0), glm::vec2{ 0, 0 }, this->fillColor);
	}

	// Number of triangles left to construct
	int trianglesLeft = pointCount - 2;
	
	// Current center point of triangle fan
	int centroid = 0;

	// Current point being added to triangle fan
	int pointIndex = 1;

	// Add triangles until all are constructed
	while (trianglesLeft) {
		if (angles[pointIndex] > PI) {
			// Switch centroid if angle is too large
			centroid = pointIndex;
		}
		else {
			// Add triangle
			this->meshBuffer.indices.emplace_back(centroid);
			this->meshBuffer.indices.emplace_back(pointIndex);
			this->meshBuffer.indices.emplace_back(IND(pointIndex + 1));

			// Increase index range
			this->currentIndexRange->upper += 3;

			// Triangle is done
			trianglesLeft--;
		}

		pointIndex = (pointIndex + 1) % pointCount;
	}

}

void xr::RenderBatch::fillTriangleFan(const std::vector<glm::vec2>& points)
{
	int pointCount = points.size();


	// Get the index of the last vertex and start indexing from there
	int startIndex = this->meshBuffer.vertices.size();


	// Add vertices
	for (int i = 0; i < pointCount; i++) {
		this->meshBuffer.vertices.emplace_back(glm::vec3(points[i], 0), glm::vec2{ 0, 0 }, this->fillColor);
	}

	// Add indices
	for (int i = 1; i < pointCount - 1; i++) {
		this->meshBuffer.indices.emplace_back(startIndex + 0);
		this->meshBuffer.indices.emplace_back(startIndex + i);
		this->meshBuffer.indices.emplace_back(startIndex + i + 1);
		
		this->currentIndexRange->upper += 3;
	}
}

void xr::RenderBatch::fillCircle(float x, float y, float r, int segments)
{
	// Get the index of the last vertex and start indexing from there
	int startIndex = this->meshBuffer.vertices.size();

	// Add vertices
	this->meshBuffer.vertices.reserve(startIndex + segments + 1);
	this->meshBuffer.vertices.emplace_back(glm::vec3(x, y, 0), glm::vec2{ 0, 0 }, this->fillColor);
	for (int i = 0; i < segments; i++)
	{
		float dx = r * cos(2 * float(PI) * i / float(segments));
		float dy = r * sin(2 * float(PI) * i / float(segments));
		this->meshBuffer.vertices.emplace_back(glm::vec3(x + dx, y + dy, 0), glm::vec2{ 0, 0 }, this->fillColor);
	}

	this->meshBuffer.indices.reserve(this->meshBuffer.indices.size() + segments * 3);
	for (int i = 0; i < segments; i++) {
		this->meshBuffer.indices.emplace_back(startIndex + 0);
		this->meshBuffer.indices.emplace_back(startIndex + i + 1);
		this->meshBuffer.indices.emplace_back(startIndex + 1 + (i + 1) % segments);

		this->currentIndexRange->upper += 3;
	}
}



void xr::RenderBatch::drawLine(float x0, float y0, float x1, float y1, float width)
{
	// Find the direction of the line
	glm::vec2 dir = glm::normalize(glm::vec2{ x1 - x0, y1 - y0 });

	// Find the perpendicular line
	glm::vec2 perp = { -dir.y, dir.x };

	// Find the corners
	glm::vec2 a = glm::vec2{ x0, y0 } + perp * width / 2.f;
	glm::vec2 b = glm::vec2{ x0, y0 } - perp * width / 2.f;
	glm::vec2 c = glm::vec2{ x1, y1 } + perp * width / 2.f;
	glm::vec2 d = glm::vec2{ x1, y1 } - perp * width / 2.f;

	
	// Get the index of the last vertex and start indexing from there
	int startIndex = this->meshBuffer.vertices.size();

	// Add vertices
	this->meshBuffer.vertices.emplace_back(glm::vec3(a, 0), glm::vec2(0), this->fillColor);
	this->meshBuffer.vertices.emplace_back(glm::vec3(b, 0), glm::vec2(0), this->fillColor);
	this->meshBuffer.vertices.emplace_back(glm::vec3(c, 0), glm::vec2(0), this->fillColor);
	this->meshBuffer.vertices.emplace_back(glm::vec3(d, 0), glm::vec2(0), this->fillColor);

	this->meshBuffer.indices.emplace_back(startIndex);
	this->meshBuffer.indices.emplace_back(startIndex + 1);
	this->meshBuffer.indices.emplace_back(startIndex + 2);
	this->meshBuffer.indices.emplace_back(startIndex + 2);
	this->meshBuffer.indices.emplace_back(startIndex + 3);
	this->meshBuffer.indices.emplace_back(startIndex + 1);

	this->currentIndexRange->upper += 6;
}


void xr::RenderBatch::fillTriangles(const std::vector<glm::vec2>& points) 
{
	// Get the index of the last vertex and start indexing from there
	int startIndex = this->meshBuffer.vertices.size();

	for (auto& point : points)
	{
		this->meshBuffer.vertices.emplace_back(glm::vec3(point, 0), glm::vec2{ 0, 0 }, this->fillColor);
		this->meshBuffer.indices.emplace_back(startIndex);
		startIndex++;

		this->currentIndexRange->upper++;
	}
}
