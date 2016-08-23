// Tube ID: 26mm
// Tube OD: 32mm 
// PCB width = 1.6mm


$fn=100;

module topBall() {
    difference() {
        union() {
            sphere(16);
            cylinder(d=10,h=7.2);
        }
        union() {
            translate([-16,-16,0]) {
                cube([32, 32, 32], centre=true);
            }
        }
    }
}


difference() {
    union() {
        topBall();
        // sloped cylinder to help fit
        cylinder(d1=25.8, d2=25, h=10);
        translate([0,0,10]) {
            cylinder(d1=25, d2=20, h=5);
        }
    }
    union() {
        translate([0,0,-20]) {
            #cylinder(d=10, h=40);
        }
    }
}