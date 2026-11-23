"""Street-placement accounting and road-relative sites for mixed native journeys.

The ratio counts new construction gestures, not graph raster edges or junction
angles. Upgrades and rejected previews do not earn a slot.
"""
import math


def runs(points):
    """Split an ordered cardinal path into contiguous straight pipe trenches."""
    if len(points) < 2:
        return [(points[0], points[0])] if points else []
    result=[]
    start=points[0]
    direction=None
    for a,b in zip(points,points[1:]):
        step=(b[0]-a[0],b[1]-a[1])
        if direction is not None and step != direction:
            result.append((start,a));start=a
        direction=step
    result.append((start,points[-1]))
    return result


def cubic(points,t):
    weights=((1-t)**3,3*(1-t)**2*t,3*(1-t)*t*t,t**3)
    return tuple(sum(w*p[axis] for w,p in zip(weights,points)) for axis in (0,1))


class StreetLayout:
    def __init__(self, profile="mixed", records=None):
        self.profile=profile
        self.records=list(records or [])

    def existing(self,a,b):
        return next((r for r in self.records if
            {tuple(r['start']),tuple(r['end'])}=={tuple(a),tuple(b)}),None)

    def next_layout(self):
        return "curve" if self.profile=="mixed" and len(self.records)%2 else "grid"

    def site(self,p):
        """Follow the nearest nominal street's actual saved geometry, within 2 tiles."""
        best=None
        for r in self.records:
            a,b=r['start'],r['end']
            dx,dy=b[0]-a[0],b[1]-a[1]
            length=dx*dx+dy*dy
            t=max(0,min(1,((p[0]-a[0])*dx+(p[1]-a[1])*dy)/length))
            nominal=(a[0]+t*dx,a[1]+t*dy)
            distance=(p[0]-nominal[0])**2+(p[1]-nominal[1])**2
            if distance>4 or best is not None and distance>=best[0]:continue
            if r.get('curve'):
                actual=cubic(r['curve'],t)
                displacement=(actual[0]-(nominal[0]+.5),actual[1]-(nominal[1]+.5))
            else:
                # Grid connector endpoints can shift to meet a curved street.
                u,v=r['physical_start'],r['physical_end']
                displacement=((1-t)*(u[0]-a[0])+t*(v[0]-b[0]),
                              (1-t)*(u[1]-a[1])+t*(v[1]-b[1]))
            # The rendered swept footprint can contain tiles without graph
            # nodes. Connect to the saved routing path, not a rounded curve pixel.
            if r.get('path'):
                target=(nominal[0]+displacement[0],nominal[1]+displacement[1])
                node=min(r['path'],key=lambda q:(q[0]-target[0])**2+(q[1]-target[1])**2)
                displacement=(node[0]-nominal[0],node[1]-nominal[1])
            best=(distance,displacement)
        if best is None:return tuple(p)
        return tuple(math.floor(p[i]+.5+best[1][i]) for i in (0,1))

    def summary(self):
        grid=sum(r['layout']=='grid' for r in self.records)
        curved=len(self.records)-grid
        return {'profile':self.profile,'count_basis':'new street placements; excludes upgrades, retries and cancellations',
                'grid':grid,'non_right_angle':curved,'grid_fraction':grid/len(self.records) if self.records else None,
                'balanced':bool(self.records) and grid==curved,'streets':self.records}
