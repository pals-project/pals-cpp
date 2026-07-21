// Low-level floor (global) coordinate geometry for lattice expansion. See
// pals_floor.h for the interface and the mapping onto the PALS "Floor
// Coordinates" / "Lattice Element Positioning" sections. Orientations are
// carried as unit quaternions rather than 3x3 rotation matrices; the only place
// a matrix appears is the internal angle<->quaternion conversion, which needs
// three specific matrix entries.

#include "pals_floor.h"

#include <cmath>

namespace pals {

Quat quat_mul(const Quat& a, const Quat& b) {
    return Quat{
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
    };
}

Quat quat_normalize(const Quat& q) {
    double n = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
    if (n <= 0.0) return Quat{1.0, 0.0, 0.0, 0.0};
    return Quat{q.w / n, q.x / n, q.y / n, q.z / n};
}

Vec3 quat_rotate(const Quat& q, const Vec3& v) {
    // v' = q * (0,v) * conj(q), expanded. Equivalent to W*v where W is the
    // matrix form of q.
    double ww = q.w, xx = q.x, yy = q.y, zz = q.z;
    double r00 = 1 - 2 * (yy * yy + zz * zz);
    double r01 = 2 * (xx * yy - ww * zz);
    double r02 = 2 * (xx * zz + ww * yy);
    double r10 = 2 * (xx * yy + ww * zz);
    double r11 = 1 - 2 * (xx * xx + zz * zz);
    double r12 = 2 * (yy * zz - ww * xx);
    double r20 = 2 * (xx * zz - ww * yy);
    double r21 = 2 * (yy * zz + ww * xx);
    double r22 = 1 - 2 * (xx * xx + yy * yy);
    return Vec3{
        r00 * v.x + r01 * v.y + r02 * v.z,
        r10 * v.x + r11 * v.y + r12 * v.z,
        r20 * v.x + r21 * v.y + r22 * v.z,
    };
}

Quat quat_from_axis_angle(const Vec3& axis, double angle) {
    double n = std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
    if (n <= 0.0) return Quat{1.0, 0.0, 0.0, 0.0};
    double h = 0.5 * angle;
    double s = std::sin(h) / n;
    return Quat{std::cos(h), axis.x * s, axis.y * s, axis.z * s};
}

Quat quat_rot_x(double angle) { return quat_from_axis_angle(Vec3{1, 0, 0}, angle); }
Quat quat_rot_y(double angle) { return quat_from_axis_angle(Vec3{0, 1, 0}, angle); }
Quat quat_rot_z(double angle) { return quat_from_axis_angle(Vec3{0, 0, 1}, angle); }

Quat quat_from_floor_angles(const FloorAngles& a) {
    // W = R_y(theta) * R_x(phi) * R_z(psi); the quaternion product mirrors that
    // matrix product order.
    return quat_normalize(
        quat_mul(quat_rot_y(a.theta), quat_mul(quat_rot_x(a.phi), quat_rot_z(a.psi))));
}

FloorAngles floor_angles_from_quat(const Quat& q) {
    Quat u = quat_normalize(q);
    double ww = u.w, xx = u.x, yy = u.y, zz = u.z;

    // Only the entries used to invert Eq. www are formed. The z-axis column of W
    // is (sin(theta)cos(phi), -sin(phi), cos(theta)cos(phi)); row 2 is
    // (cos(phi)sin(psi), cos(phi)cos(psi), -sin(phi)).
    double r02 = 2 * (xx * zz + ww * yy);
    double r12 = 2 * (yy * zz - ww * xx);
    double r22 = 1 - 2 * (xx * xx + yy * yy);
    double r10 = 2 * (xx * yy + ww * zz);
    double r11 = 1 - 2 * (xx * xx + zz * zz);

    FloorAngles a;
    double cos_phi = std::sqrt(r02 * r02 + r22 * r22);
    a.phi = std::atan2(-r12, cos_phi);
    if (cos_phi > 1e-12) {
        a.theta = std::atan2(r02, r22);
        a.psi = std::atan2(r10, r11);
    } else {
        // Gimbal lock (phi = +-pi/2): theta and psi share an axis. Pin psi = 0
        // and let theta carry the whole rotation. With psi = 0, W row 0 reduces
        // to (cos(theta), *, *) and W[0][0] = r00, W[2][0] = r20.
        double r00 = 1 - 2 * (yy * yy + zz * zz);
        double r20 = 2 * (xx * zz - ww * yy);
        a.theta = std::atan2(-r20, r00);
        a.psi = 0.0;
    }
    return a;
}

FloorState floor_propagate(const FloorState& s0, const Vec3& L, const Quat& S) {
    Vec3 dr = quat_rotate(s0.q, L);
    FloorState s1;
    s1.r = Vec3{s0.r.x + dr.x, s0.r.y + dr.y, s0.r.z + dr.z};
    s1.q = quat_normalize(quat_mul(s0.q, S));
    return s1;
}

void straight_LS(double length, Vec3& L, Quat& S) {
    L = Vec3{0.0, 0.0, length};
    S = Quat{1.0, 0.0, 0.0, 0.0};
}

void bend_LS(double length, double angle, double tilt_ref, Vec3& L, Quat& S) {
    // A bend with no angle bends nothing: fall back to a straight segment so a
    // zero-curvature "bend" (or a numerically tiny angle) stays well defined.
    if (std::fabs(angle) < 1e-12) {
        straight_LS(length, L, S);
        return;
    }
    double rho = length / angle;  // rho = length / angle_ref
    // Displacement in the un-tilted bend plane (Eq. lrztt, L~) then rotated by
    // tilt_ref about z.
    Vec3 L_tilde{rho * (std::cos(angle) - 1.0), 0.0, rho * std::sin(angle)};
    L = quat_rotate(quat_rot_z(tilt_ref), L_tilde);
    // Rotation axis u = (-sin(tilt_ref), -cos(tilt_ref), 0), angle = angle_ref
    // (Eq. ustt). u is a unit vector.
    S = quat_from_axis_angle(Vec3{-std::sin(tilt_ref), -std::cos(tilt_ref), 0.0},
                             angle);
}

void patch_LS(double x_offset, double y_offset, double z_offset, double x_rot,
              double y_rot, double z_rot, Vec3& L, Quat& S) {
    L = Vec3{x_offset, y_offset, z_offset};
    // S = R_y(y_rot) * R_x(x_rot) * R_z(z_rot)  (Eq. lxyz).
    S = quat_normalize(
        quat_mul(quat_rot_y(y_rot), quat_mul(quat_rot_x(x_rot), quat_rot_z(z_rot))));
}

}  // namespace pals
