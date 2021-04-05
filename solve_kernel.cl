__kernel void solve_kernel(long width,
                           long segWidth,
                           long startX,
                           long startY,
                           __global int *rows,
                           __global int *changed) {        
    // minus two then f or g(x) then plus one to ignore 1px border
    __private long global_idx = (long) get_global_id(0);
    __private long x = (global_idx % segWidth) + startX;
    __private long y = (global_idx / segWidth) + startY;
        
    if (rows[y * width + x] != 0) {    
        __private int count;
        #define FN \
            count = rows[y * width + x - 1] != 0;\
            count += rows[y * width + x + 1] != 0;\
            count += rows[(y - 1) * width + x] != 0;\
            count += rows[(y + 1) * width + x] != 0;\
            \
            if (count < 2) {\
                rows[y * width + x] = 0;\
                *changed = 1;\
                return;\
            }
        
        FN
        FN
        FN
        FN
        FN
        
        FN
        FN
        FN
        FN
        FN    
    }
}

__kernel void set_changed_to_zero(__global int *changed) {    
    *changed = 0;
}
