// The height of the base.
baseHeight = 20;
baseRadius = 65; 

// Size of the clear acrylic tube
tubeSize = 32; 
// How far down into the base the tube is expected to go
tubeDepthInBase = baseHeight - 10;

// LiPol cell
//batteryBoxWidth = 37; 
//batteryBoxLength = 54;
//batteryBoxDepth = 13;

// AAA battery.
batteryBoxWidth = 26;
batteryBoxLength = 54;
batteryBoxDepth = 14;



// series of releafes in the base to allow water to run through
// for the water detection part
module waterHoles() {
    for (angle = [30 : 30 : 150]) {
        rotate(a=[angle,90,0]) {
            cylinder(d=6, h=baseRadius+10);
        }
    }
    
    for (angle = [210 : 30 : 330]) {
        rotate(a=[angle,90,0]) {
            cylinder(d=6, h=baseRadius+10);
        }
    }  
}

module batteryCompartment(isTop) {
    translate([(tubeSize/2)+5,-(batteryBoxLength/2), (baseHeight-batteryBoxDepth)]) {
        cube([batteryBoxWidth,batteryBoxLength,batteryBoxDepth]);
        translate([-23,0,0]) {
            //cube([24,5,batteryBoxDepth]);
            translate([0,0,-1]) {
                //cube([5,16,batteryBoxDepth+1]);
            }
        }
        
        if (isTop) {
            translate([4,3.5,0]) {
                rotate(a=135) {
                    cube([28,5,batteryBoxDepth]);
                }
            }
        } else {
            
            translate([0,batteryBoxLength,0]) {
                rotate(a=225) {
                    cube([28,5,batteryBoxDepth]);
                }
            }
        }
    }
}

module base() {
    difference() {
        union() {
            cylinder(r1=baseRadius+5, r2=baseRadius, h=baseHeight, $fn=80);
        }
        union() {
            waterHoles();
            
            batteryCompartment(true);
            
            rotate(a=180) {
                batteryCompartment(false);
            }
            
            // Small inner hole all the way through
            // diameter to match the PCB narrow width used for the 
            // water sensor.
            cylinder(d=22, h=baseHeight, $fn=80);
            
            // 2 vertical pockets for the PCB to rest into.
            translate([10,-1,2]) {
                #cube([4,2,baseHeight]);
            }
            translate([-14,-1,2]) {
                #cube([4,2,baseHeight]);
            }
            
            // tube stops before the bottom of the base
            // Move up so the tube comes through the top of the base
            translate([0,0,baseHeight - tubeDepthInBase]) {
                // Slight angle to give it an easy in and snug fit.
                cylinder(d1=tubeSize, d2 = tubeSize+0.5, h=tubeDepthInBase, $fn=80);
            }
        }
    }
}


base();
//waterHoles();