/* Native track meshes compiled once into WebGL triangle and line buffers. */
(function (root, factory) {
    const data = typeof module === 'object' && module.exports ? require('./track-editor-scene-data.js') : root.TrackEditorSceneData;
    const resources = typeof module === 'object' && module.exports ? require('./track-editor-resources.js') : root.TrackEditorResources;
    const api = factory(data, resources);
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.TrackEditorScene = api;
}(typeof globalThis !== 'undefined' ? globalThis : this, function (data, resources) {
    'use strict';
    const TILE_SIZE = 1024, LEVEL_HEIGHT = 450, HALF_TILE = TILE_SIZE / 2;
    const PRIMITIVE_COUNTS = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 2, 6, 3, 0, 0];
    const SHAPE_HEADER_BYTES = 4, VERTEX_BYTES = 6, MASK_BYTES = 8, PALETTE_CHANNELS = 3;
    const MAX_DIMENSION = 90, FIRST_RUNTIME_OBJECT = 182, FIRST_SLOPE_TERRAIN = 7;
    const ROAD_MODELS = new Set([1, 16, 17]), UPRIGHT_MODELS = new Set([71, 72, 74]);
    const CURVED_ROAD_MODELS = new Set([35, 27, 29, 30, 31, 22]);
    const PAVED = 19, PAVED_MARKING = 21, SHADED_PAVED = 22, SHADED_MARKING = 24;
    const DIRT = 25, DIRT_MARKING = 27, ICE = 28, ICE_MARKING = 30;
    const HILL_GRASS_FIRST = 101, HILL_GRASS_LAST = 105;
    const SURFACE_BIAS = .0005, MARKING_BIAS = .001, GEOMETRY_EPSILON = 1e-10;
    const FLOAT_BUFFER_CAPACITY = 16384, SPHERE_SEGMENTS = 12, SPHERE_RINGS = 6;
    const PINE_COUNTS = new Set([6, 10]), PINE_SCALE = 256;
    const WHEAT_FIELD_ID = 215, WHEAT_PHYSICAL_MODEL = 75;
    const TULIP_FIELD_ID = 216, TULIP_PHYSICAL_MODEL = 76, VINE_FIELD_ID = 217, VINE_PHYSICAL_MODEL = 77;
    // Keep the generated crop mesh in sync with extended/extended_wheat_field.c.
    const WHEAT = { rows: 6, soilHeight: 2, canopyHeight: 10, rowHalfWidth: 70, soil: 25, canopy: 82 };
    // Match the solid crop rows in extended/extended_crop_fields.c.
    const TULIP = { rows: 8, leafHeight: 4, rowHalfWidth: 60, bandHeight: 12, bandHalfWidth: 50,
        leaves: 69, rowColors: [15, 10, 61, 124, 54, 75, 37, 9] };
    const VINE = { rows: 12, height: 96, eaveHeight: 72, ridge: 0, left: -22, right: 23,
        dark: 68, light: 69, grapes: 73, grapeClearance: 2, grapeStationCount: 8,
        grapeParts: [{ halfWidth: 16, bottom: 48, top: 72 }, { halfWidth: 8, bottom: 32, top: 48 }] };
    const ROLL = { length: 2048, halfWidth: 120, ease: 128, rise: 390, segments: 32,
        lipBank: Math.floor(1024 * 71 / 360) * Math.PI * 2 / 1024,
        concrete: 31, concreteLight: 32, grille: 0, markings: 12, dashesPerTile: 6,
        dashLength: 85, dashHalfWidth: 2, dashClearance: 6, supportX: 190, supportHalf: 20, beamHeight: 24 };
    const decodedShapes = new WeakMap();
    function fail(message) { throw new Error(message); }
    function decodeShape(input) {
        if (!(input instanceof Uint8Array) || input.length < SHAPE_HEADER_BYTES) fail('Truncated 3D model header.');
        if (decodedShapes.has(input)) return decodedShapes.get(input);
        const count = input[0], primitiveCount = input[1], paintCount = input[2];
        if (!count || !paintCount) fail('Invalid 3D model vertex or paint count.');
        let cursor = SHAPE_HEADER_BYTES + count * VERTEX_BYTES + primitiveCount * MASK_BYTES;
        if (cursor > input.length) fail('Truncated 3D model vertices or visibility masks.');
        const view = new DataView(input.buffer, input.byteOffset, input.byteLength), vertices = [], primitives = [];
        for (let index = 0; index < count; index++) {
            const offset = SHAPE_HEADER_BYTES + index * VERTEX_BYTES;
            vertices.push([view.getInt16(offset, true), view.getInt16(offset + 2, true), view.getInt16(offset + 4, true)]);
        }
        for (let index = 0; index < primitiveCount; index++) {
            const type = input[cursor], size = PRIMITIVE_COUNTS[type];
            if (!size || cursor + 2 + paintCount + size > input.length) fail('Invalid or truncated 3D model primitive.');
            const materials = Array.from(input.subarray(cursor + 2, cursor + 2 + paintCount));
            const indices = Array.from(input.subarray(cursor + 2 + paintCount, cursor + 2 + paintCount + size));
            if (indices.some(vertex => vertex >= count)) fail('3D model primitive references an invalid vertex.');
            primitives.push({ type, flags: input[cursor + 1], materials, indices });
            cursor += 2 + paintCount + size;
        }
        const shape = { vertices, primitives };
        decodedShapes.set(input, shape);
        return shape;
    }
    class FloatBuffer {
        constructor() { this.values = new Float32Array(FLOAT_BUFFER_CAPACITY); this.length = 0; }
        point(point) {
            if (this.length + PALETTE_CHANNELS > this.values.length) {
                const next = new Float32Array(this.values.length * 2); next.set(this.values); this.values = next;
            }
            this.values.set(point, this.length); this.length += PALETTE_CHANNELS;
        }
        finish() { return this.values.slice(0, this.length); }
    }
    function rotate(point, turns) {
        const [x, y, z] = point;
        switch ((turns % 4 + 4) % 4) {
            case 1: return [z, y, -x];
            case 2: return [-x, y, -z];
            case 3: return [-z, y, x];
            default: return [x, y, z];
        }
    }
    function materialColor(material, palette) {
        const index = (data.materials[material] || data.materials[0]).color * PALETTE_CHANNELS;
        return [palette[index] / 255, palette[index + 1] / 255, palette[index + 2] / 255];
    }
    function cross(first, second, third) {
        const a = second.map((value, axis) => value - first[axis]), b = third.map((value, axis) => value - first[axis]);
        return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
    }
    function triangulate(points) {
        if (points.length === 3) return [points];
        const normal = cross(points[0], points[1], points[2]);
        const drop = normal.map(Math.abs).indexOf(Math.max(...normal.map(Math.abs)));
        const axes = [0, 1, 2].filter(axis => axis !== drop), polygon = points.map((point, index) => ({ point, index }));
        const area2 = (a, b, c) => (b[axes[0]] - a[axes[0]]) * (c[axes[1]] - a[axes[1]]) -
            (b[axes[1]] - a[axes[1]]) * (c[axes[0]] - a[axes[0]]);
        let area = 0;
        for (let index = 0; index < points.length; index++) {
            const next = points[(index + 1) % points.length];
            area += points[index][axes[0]] * next[axes[1]] - next[axes[0]] * points[index][axes[1]];
        }
        const orientation = area < 0 ? -1 : 1, triangles = [];
        while (polygon.length > 3) {
            let found = false;
            for (let index = 0; index < polygon.length; index++) {
                const a = polygon[(index + polygon.length - 1) % polygon.length].point;
                const b = polygon[index].point, c = polygon[(index + 1) % polygon.length].point;
                if (area2(a, b, c) * orientation <= GEOMETRY_EPSILON) continue;
                if (polygon.some(entry => entry.point !== a && entry.point !== b && entry.point !== c &&
                    area2(a, b, entry.point) * orientation >= -GEOMETRY_EPSILON &&
                    area2(b, c, entry.point) * orientation >= -GEOMETRY_EPSILON &&
                    area2(c, a, entry.point) * orientation >= -GEOMETRY_EPSILON)) continue;
                triangles.push([a, b, c]); polygon.splice(index, 1); found = true; break;
            }
            if (!found) {
                // A few stock quads have collinear or slightly non-planar edges.
                // Preserve their authored diagonal rather than dropping the face.
                for (let index = 1; index + 1 < polygon.length; index++)
                    triangles.push([polygon[0].point, polygon[index].point, polygon[index + 1].point]);
                return triangles;
            }
        }
        triangles.push(polygon.map(entry => entry.point)); return triangles;
    }
    function clipPolygon(points, distance, keepInside) {
        const output = [];
        for (let index = 0; index < points.length; index++) {
            const current = points[index], previous = points[(index + points.length - 1) % points.length];
            const currentDistance = distance(current), previousDistance = distance(previous);
            const inside = keepInside ? currentDistance >= -GEOMETRY_EPSILON : currentDistance <= GEOMETRY_EPSILON;
            const previousInside = keepInside ? previousDistance >= -GEOMETRY_EPSILON : previousDistance <= GEOMETRY_EPSILON;
            if (inside !== previousInside) {
                const factor = previousDistance / (previousDistance - currentDistance);
                output.push(previous.map((value, axis) => value + (current[axis] - value) * factor));
            }
            if (inside) output.push(current);
        }
        return output;
    }
    function repaint(primitive, surface, physical) {
        if (physical >= 64) surface = 0;
        let material = primitive.materials[surface] === undefined ? primitive.materials[0] : primitive.materials[surface];
        if (surface && (primitive.materials.length <= 2 || material === primitive.materials[0])) {
            const road = surface === 1 ? DIRT : ICE;
            if (material === PAVED) material = road;
            else if (material === PAVED_MARKING) material = surface === 1 ? DIRT_MARKING : ICE_MARKING;
            else if (CURVED_ROAD_MODELS.has(physical) && material >= SHADED_PAVED && material <= SHADED_MARKING)
                material = road + material - SHADED_PAVED;
            else if ((physical === 18 || physical === 19) && material >= SHADED_PAVED && material < SHADED_MARKING)
                material = road;
        }
        return material;
    }
    function supportHeight(object, x, z) {
        return (object.baseHeight + (object.slopeX || 0) * (object.width / 2 + x / TILE_SIZE) +
            (object.slopeY || 0) * (object.height / 2 - z / TILE_SIZE)) * LEVEL_HEIGHT;
    }
    function nativeHill(object, definition) {
        if (!(object.slopeX || object.slopeY) || object.width !== 1 || object.height !== 1 || !ROAD_MODELS.has(definition.physical)) return null;
        const rotation = (definition.rotation + object.rotation) % 4;
        const terrain = object.slopeX ? (object.slopeX > 0 ? 10 : 8) : (object.slopeY > 0 ? 9 : 7);
        let id = definition.physical === 1 ? 4 + rotation % 2 : data.objects.findIndex((candidate, index) =>
            index < FIRST_RUNTIME_OBJECT && candidate.physical === definition.physical && candidate.rotation === rotation);
        const row = data.hillRoads[terrain - FIRST_SLOPE_TERRAIN], index = row.pieces.indexOf(id);
        if (index < 0) return null;
        id = row.shapes[index < 3 ? index : 3];
        return data.objects[id];
    }
    function rollSample(distance, lateral) {
        const center = distance < ROLL.ease ? ROLL.rise * distance * distance / (2 * ROLL.ease * TILE_SIZE)
            : ROLL.rise * (distance - ROLL.ease / 2) / TILE_SIZE;
        const q = Math.max(0, (distance - TILE_SIZE) / TILE_SIZE), bank = ROLL.lipBank * (2 * q * q - q * q * q);
        return [lateral * Math.cos(bank), center + lateral * Math.sin(bank), HALF_TILE * 2 - distance];
    }
    function rollRamp(surface) {
        const vertices = [], primitives = [], road = surface === 1 ? DIRT : surface === 2 ? ICE : PAVED;
        function face(points, material, wire = false) {
            const indices = points.map(point => { vertices.push(point); return vertices.length - 1; });
            primitives.push({ type: points.length, flags: 1, materials: [material], indices, wire });
        }
        const step = ROLL.length / ROLL.segments;
        for (let segment = 0; segment < ROLL.segments; segment++) {
            const along = segment * step, next = along + step;
            const left = rollSample(along, -ROLL.halfWidth), right = rollSample(along, ROLL.halfWidth);
            const nextLeft = rollSample(next, -ROLL.halfWidth), nextRight = rollSample(next, ROLL.halfWidth);
            face([left, right, nextRight], segment < ROLL.segments / 2 ? road : ROLL.grille, segment >= ROLL.segments / 2);
            face([left, nextRight, nextLeft], segment < ROLL.segments / 2 ? road : ROLL.grille, segment >= ROLL.segments / 2);
            if (segment < ROLL.segments / 2) {
                face([left, nextLeft, [nextLeft[0], 0, nextLeft[2]], [left[0], 0, left[2]]], ROLL.concrete);
                face([right, [right[0], 0, right[2]], [nextRight[0], 0, nextRight[2]], nextRight], ROLL.concreteLight);
            }
        }
        const midLeft = rollSample(TILE_SIZE, -ROLL.halfWidth), midRight = rollSample(TILE_SIZE, ROLL.halfWidth);
        face([midLeft, midRight, [midRight[0], 0, midRight[2]], [midLeft[0], 0, midLeft[2]]], ROLL.concrete);
        function box(minimum, maximum) {
            const points = Array.from({ length: 8 }, (_, corner) => [corner & 1 ? maximum[0] : minimum[0],
                corner & 2 ? maximum[1] : minimum[1], corner & 4 ? maximum[2] : minimum[2]]);
            for (const indices of [[0, 1, 3, 2], [4, 6, 7, 5], [0, 4, 5, 1], [2, 3, 7, 6], [0, 2, 6, 4], [1, 5, 7, 3]])
                face(indices.map(index => points[index]), ROLL.concrete);
        }
        for (const along of [TILE_SIZE * 1.25, TILE_SIZE * 1.75]) {
            const low = along - ROLL.supportHalf, high = along + ROLL.supportHalf;
            const top = Math.min(rollSample(low, -ROLL.halfWidth)[1], rollSample(high, -ROLL.halfWidth)[1]);
            const z = TILE_SIZE - along;
            for (const x of [-ROLL.supportX, ROLL.supportX])
                box([x - ROLL.supportHalf, 0, z - ROLL.supportHalf], [x + ROLL.supportHalf, top, z + ROLL.supportHalf]);
            box([-ROLL.supportX - ROLL.supportHalf, top - ROLL.beamHeight, z - ROLL.supportHalf],
                [ROLL.supportX + ROLL.supportHalf, top, z + ROLL.supportHalf]);
        }
        for (let dash = 0; dash < ROLL.markings; dash++) {
            const along = Math.floor(dash / ROLL.dashesPerTile) * TILE_SIZE + dash % ROLL.dashesPerTile * TILE_SIZE / ROLL.dashesPerTile;
            const points = [[along, -ROLL.dashHalfWidth], [along, ROLL.dashHalfWidth],
                [along + ROLL.dashLength, ROLL.dashHalfWidth], [along + ROLL.dashLength, -ROLL.dashHalfWidth]]
                .map(([distance, lateral]) => { const point = rollSample(distance, lateral); point[1] += ROLL.dashClearance; return point; });
            face(points, PAVED_MARKING);
        }
        return { vertices, primitives };
    }
    function cropFieldBounds(height) {
        const vertices = [0, 1, 3, 2, 7, 6, 4, 5].map(corner => [corner & 1 ? HALF_TILE : -HALF_TILE,
            corner & 2 ? height : 0, corner & 4 ? HALF_TILE : -HALF_TILE]);
        return { vertices, primitives: [] };
    }
    function cropFieldBase(height) {
        const shape = cropFieldBounds(height);
        cropFieldQuad(shape, -HALF_TILE, HALF_TILE, WHEAT.soilHeight, WHEAT.soil);
        return shape;
    }
    function cropFieldPrimitive(shape, indices, material) {
        shape.primitives.push({ type: indices.length, flags: 1, materials: [material], indices });
    }
    function cropFieldQuad(shape, left, right, height, material) {
        const first = shape.vertices.length;
        for (let corner = 0; corner < 4; corner++)
            shape.vertices.push([corner & 1 ? right : left, height, corner & 2 ? HALF_TILE : -HALF_TILE]);
        cropFieldPrimitive(shape, [0, 1, 3, 2].map(index => first + index), material);
    }
    function cropFieldCenter(index, count) {
        return Math.floor((2 * index + 1) * TILE_SIZE / (2 * count)) - HALF_TILE;
    }
    function wheatField() {
        const shape = cropFieldBase(WHEAT.canopyHeight);
        for (let row = 0; row < WHEAT.rows; row++) {
            const x = cropFieldCenter(row, WHEAT.rows);
            cropFieldQuad(shape, x - WHEAT.rowHalfWidth, x + WHEAT.rowHalfWidth, WHEAT.canopyHeight, WHEAT.canopy);
        }
        return shape;
    }
    function tulipField() {
        const shape = cropFieldBase(TULIP.bandHeight);
        for (let row = 0; row < TULIP.rows; row++) {
            const x = cropFieldCenter(row, TULIP.rows);
            cropFieldQuad(shape, x - TULIP.rowHalfWidth, x + TULIP.rowHalfWidth, TULIP.leafHeight, TULIP.leaves);
        }
        for (let row = 0; row < TULIP.rows; row++) {
            const x = cropFieldCenter(row, TULIP.rows);
            cropFieldQuad(shape, x - TULIP.bandHalfWidth, x + TULIP.bandHalfWidth, TULIP.bandHeight, TULIP.rowColors[row]);
        }
        return shape;
    }
    function vineField() {
        const shape = cropFieldBase(VINE.height);
        const section = [[VINE.left, WHEAT.soilHeight], [VINE.left, VINE.eaveHeight], [VINE.ridge, VINE.height],
            [VINE.right, VINE.eaveHeight], [VINE.right, WHEAT.soilHeight]];
        const sectionSize = section.length;
        for (let row = 0; row < VINE.rows; row++) {
            const x = cropFieldCenter(row, VINE.rows), first = shape.vertices.length;
            for (const z of [-HALF_TILE, HALF_TILE]) for (const [dx, height] of section)
                shape.vertices.push([x + dx, height, z]);
            for (let segment = 0; segment < sectionSize - 1; segment++)
                cropFieldPrimitive(shape, [segment, segment + 1, segment + sectionSize + 1, segment + sectionSize].map(index => first + index),
                    segment === 0 || segment === sectionSize - 2 ? VINE.dark : VINE.light);
            cropFieldPrimitive(shape, Array.from({ length: sectionSize }, (_, index) => first + index), VINE.dark);
            cropFieldPrimitive(shape, Array.from({ length: sectionSize }, (_, index) => first + (index ? 2 * sectionSize - index : sectionSize)), VINE.dark);
        }
        return shape;
    }
    function vineGrapes(row) {
        const shape = cropFieldBounds(VINE.height), center = cropFieldCenter(row, VINE.rows);
        for (let station = 0; station < VINE.grapeStationCount; station++) {
            const z = cropFieldCenter(station, VINE.grapeStationCount);
            for (const side of [-1, 1]) for (const part of VINE.grapeParts) {
                const first = shape.vertices.length;
                for (const [dz, height] of [[-part.halfWidth, part.bottom], [part.halfWidth, part.bottom], [part.halfWidth, part.top], [-part.halfWidth, part.top]]) {
                    shape.vertices.push([center + (side < 0 ? VINE.left - VINE.grapeClearance : VINE.right + VINE.grapeClearance), height, z + dz]);
                }
                cropFieldPrimitive(shape, (side < 0 ? [0, 1, 2, 3] : [0, 3, 2, 1]).map(index => first + index), VINE.grapes);
            }
        }
        return shape;
    }
    const wheatFieldShape = wheatField();
    const cropFieldShapes = new Map([
        [WHEAT_FIELD_ID, { name: 'wheat field', shapes: [wheatFieldShape], physical: WHEAT_PHYSICAL_MODEL }],
        [TULIP_FIELD_ID, { name: 'tulip field', shapes: [tulipField()], physical: TULIP_PHYSICAL_MODEL }],
        [VINE_FIELD_ID, { name: 'vine field', shapes: [vineField(), ...Array.from({ length: VINE.rows }, (_, row) => vineGrapes(row))], physical: VINE_PHYSICAL_MODEL }]
    ]);
    function buildScene(doc, assets) {
        if (!doc || !Number.isInteger(doc.width) || !Number.isInteger(doc.height) || doc.width < 1 || doc.height < 1 ||
            doc.width > MAX_DIMENSION || doc.height > MAX_DIMENSION || !doc.vertices || !Array.isArray(doc.cells) || !Array.isArray(doc.objects))
            fail('Invalid track for 3D preview.');
        if (!assets || !(assets.models instanceof Map) || !assets.palette || assets.palette.length < 256 * PALETTE_CHANNELS)
            fail('Connect the original Stunts 3D resources before opening the preview.');
        const positions = new FloatBuffer(), colors = new FloatBuffer(), linePositions = new FloatBuffer(), lineColors = new FloatBuffer();
        const bounds = { min: [0, 0, 0], max: [doc.width, 0, doc.height] };
        let instances = 0;
        function trackBounds(point) {
            for (let axis = 0; axis < 3; axis++) { bounds.min[axis] = Math.min(bounds.min[axis], point[axis]); bounds.max[axis] = Math.max(bounds.max[axis], point[axis]); }
        }
        function polygon(points, material, bias = 0) {
            if (points.length < 3) return;
            const color = materialColor(material, assets.palette);
            for (const triangle of triangulate(points)) {
                const normal = cross(...triangle), length = Math.hypot(...normal);
                if (length <= GEOMETRY_EPSILON) continue;
                const light = .78 + .22 * Math.abs(normal[1]) / length, shaded = color.map(channel => channel * light);
                for (const point of triangle) { const raised = [point[0], point[1] + bias, point[2]]; positions.point(raised); colors.point(shaded); trackBounds(raised); }
            }
        }
        function line(first, second, material) {
            const color = materialColor(material, assets.palette);
            linePositions.point(first); linePositions.point(second); lineColors.point(color); lineColors.point(color);
            trackBounds(first); trackBounds(second);
        }
        function terrainTile(column, row) {
            const stride = doc.width + 1, first = row * stride + column;
            const heights = [doc.vertices[first], doc.vertices[first + 1], doc.vertices[first + stride + 1], doc.vertices[first + stride]];
            const points = [[column, heights[0] * LEVEL_HEIGHT / TILE_SIZE, doc.height - row],
                [column + 1, heights[1] * LEVEL_HEIGHT / TILE_SIZE, doc.height - row],
                [column + 1, heights[2] * LEVEL_HEIGHT / TILE_SIZE, doc.height - row - 1],
                [column, heights[3] * LEVEL_HEIGHT / TILE_SIZE, doc.height - row - 1]];
            const triangles = heights[0] !== heights[2] ? [[0, 1, 3], [1, 2, 3]] : [[0, 1, 2], [0, 2, 3]];
            const cell = doc.cells[row * doc.width + column], corner = cell.halfWaterCorner || 0;
            for (const indices of triangles) {
                const face = indices.map(index => points[index]);
                if (!corner) { polygon(face, data.groundMaterials[cell.surface]); continue; }
                const distance = point => {
                    const x = point[0] - column, y = doc.height - row - point[2];
                    return corner === 1 ? 1 - x - y : corner === 2 ? x - y : corner === 3 ? x + y - 1 : y - x;
                };
                polygon(clipPolygon(face, distance, false), data.groundMaterials[cell.surface]);
                polygon(clipPolygon(face, distance, true), data.groundMaterials[cell.cornerSurface === undefined ? 3 : cell.cornerSurface]);
            }
        }
        for (let row = 0; row < doc.height; row++) for (let column = 0; column < doc.width; column++) terrainTile(column, row);
        function nativeShape(name) {
            const bytes = assets.models.get(name);
            if (!bytes) fail('The Stunts folder is missing 3D model “' + name + '”.');
            return decodeShape(bytes);
        }
        function mesh(object, definition, shape, physical, surface, settings = {}) {
            const rotation = settings.rotation === undefined ? (definition.rotation + object.rotation) % 4 : settings.rotation;
            const scale = settings.scale || 1, offset = settings.offset || [0, 0, 0];
            const local = shape.vertices.map(point => rotate(point, rotation).map((value, axis) => Math.trunc(value * scale) + offset[axis]));
            const upright = UPRIGHT_MODELS.has(physical) && (object.slopeX || object.slopeY) && !settings.nativeHill;
            let anchor = local[0];
            if (upright) for (const point of local)
                if ((object.slopeX || 0) * point[0] - (object.slopeY || 0) * point[2] > (object.slopeX || 0) * anchor[0] - (object.slopeY || 0) * anchor[2]) anchor = point;
            const nativeBase = (object.baseHeight + Math.min(0, (object.slopeX || 0) * object.width) +
                Math.min(0, (object.slopeY || 0) * object.height)) * LEVEL_HEIGHT;
            const world = local.map(point => {
                const support = upright && point[1] ? anchor : point;
                const height = settings.nativeHill ? nativeBase : supportHeight(object, support[0], support[2]);
                return [object.x + object.width / 2 + point[0] / TILE_SIZE, (point[1] + height) / TILE_SIZE,
                    doc.height - object.y - object.height / 2 + point[2] / TILE_SIZE];
            });
            for (const primitive of shape.primitives) {
                const material = repaint(primitive, surface, physical), points = primitive.indices.map(index => world[index]);
                if (settings.nativeHill && material >= HILL_GRASS_FIRST && material <= HILL_GRASS_LAST) continue;
                const bias = material === PAVED_MARKING || material === DIRT_MARKING || material === ICE_MARKING || material === SHADED_MARKING
                    ? MARKING_BIAS : definition.ignoreZBias ? SURFACE_BIAS : 0;
                if (primitive.type === 1) {
                    const point = points[0]; line(point, [point[0], point[1] + SURFACE_BIAS, point[2]], material);
                } else if (primitive.type === 2) line(points[0], points[1], material);
                else if (primitive.wire) for (let index = 0; index < points.length; index++) line(points[index], points[(index + 1) % points.length], material);
                else if (primitive.type === 11) {
                    const center = points[0], radius = Math.hypot(...points[1].map((value, axis) => value - center[axis]));
                    const sample = (ring, segment) => [center[0] + radius * Math.sin(ring * Math.PI / SPHERE_RINGS) * Math.cos(segment * Math.PI * 2 / SPHERE_SEGMENTS),
                        center[1] + radius * Math.cos(ring * Math.PI / SPHERE_RINGS), center[2] + radius * Math.sin(ring * Math.PI / SPHERE_RINGS) * Math.sin(segment * Math.PI * 2 / SPHERE_SEGMENTS)];
                    for (let ring = 0; ring < SPHERE_RINGS; ring++) for (let segment = 0; segment < SPHERE_SEGMENTS; segment++)
                        polygon([sample(ring, segment), sample(ring + 1, segment), sample(ring + 1, segment + 1), sample(ring, segment + 1)], material);
                } else polygon(points, material, bias);
            }
            instances++;
        }
        for (const object of doc.objects) {
            const field = cropFieldShapes.get(object.id);
            if (field) {
                if (object.width !== 1 || object.height !== 1 || object.mask !== 1 ||
                    object.lowerSurface || object.tunnelSlalom || object.rollRamp || object.pineCount) fail('Invalid 3D ' + field.name + '.');
                for (const shape of field.shapes) mesh(object, { rotation: 0, ignoreZBias: 0 }, shape, field.physical, 0);
                continue;
            }
            const original = data.objects[object.id];
            if (!original || !original.shape) fail('Unsupported 3D track element ' + object.id + '.');
            if (object.pineCount) {
                if (!PINE_COUNTS.has(object.pineCount) || original.physical !== 71) fail('Invalid 3D pine group.');
                const shape = nativeShape(original.shape), layout = resources.pineTreeLayout(object.pineCount, object.x, object.y);
                for (const [x, z, scale] of layout) mesh(object, original, shape, original.physical, 0,
                    { scale: scale / PINE_SCALE, offset: rotate([x, 0, z], object.rotation) });
                continue;
            }
            if (object.rollRamp) { mesh(object, original, rollRamp(object.surface), original.physical, 0); continue; }
            if (object.tunnelSlalom) {
                const tunnel = data.objects[66], floor = data.objects[tunnel.overlay];
                const shell = { ...tunnel, rotation: original.rotation }, base = { ...floor, rotation: original.rotation };
                mesh(object, shell, nativeShape(shell.shape), original.physical, object.surface);
                mesh(object, original, nativeShape(original.shape), original.physical, object.surface);
                mesh(object, base, nativeShape(base.shape), original.physical, object.surface);
                continue;
            }
            const hill = nativeHill(object, original), definition = hill || original;
            const options = hill ? { nativeHill: true, rotation: definition.rotation } : {};
            mesh(object, definition, nativeShape(definition.shape), original.physical, object.surface, options);
            if (definition.overlay) {
                const overlay = definition.overlay < data.objects.length ? data.objects[definition.overlay] : data.terrain[definition.overlay - data.objects.length];
                const selection = original.physical === 22 ? object.lowerSurface || 0 : object.surface;
                const overlayOptions = hill ? { nativeHill: true, rotation: overlay.rotation } : {};
                mesh(object, overlay, nativeShape(overlay.shape), original.physical, selection, overlayOptions);
            }
        }
        const start = doc.objects.find(object => data.objects[object.id] && data.objects[object.id].physical === 0);
        const centerX = start ? start.x + start.width / 2 : doc.width / 2;
        const centerZ = start ? doc.height - start.y - start.height / 2 : doc.height / 2;
        const centerTileX = Math.min(doc.width - 1, Math.max(0, Math.floor(centerX)));
        const centerTileY = Math.min(doc.height - 1, Math.max(0, Math.floor(doc.height - centerZ)));
        const stride = doc.width + 1, vertex = centerTileY * stride + centerTileX;
        const ground = (doc.vertices[vertex] + doc.vertices[vertex + 1] + doc.vertices[vertex + stride] + doc.vertices[vertex + stride + 1]) / 4;
        return { positions: positions.finish(), colors: colors.finish(), linePositions: linePositions.finish(), lineColors: lineColors.finish(), bounds,
            spawn: { position: [centerX, ground * LEVEL_HEIGHT / TILE_SIZE + .18, centerZ], yaw: start ? ((data.objects[start.id].rotation + start.rotation) % 4) * Math.PI / 2 : 0, pitch: 0 },
            triangles: positions.length / 9, instances };
    }
    return { buildScene, decodeShape, triangulate, TILE_SIZE, LEVEL_HEIGHT };
}));
