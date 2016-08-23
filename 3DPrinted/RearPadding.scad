// Tube ID 26mm
// PCB width = 1.6mm
// == 24.4mm to cover.
// /2 = 12.2mm padding either side of the PCB.
//
// 

$fn=100;
difference() {
    union() {
        // 14mm between PCB and tube.
        sphere(5); // 5mm rad. 
        cylinder(d=10,h=7.2); // 12.2 - 5 == 7.2
    }

    union() {
        translate([0,0,2]) {
            #cylinder(d=4,h=12.2);
        }
    }
}