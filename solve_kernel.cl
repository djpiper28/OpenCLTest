__kernel void solve_kernel(long width,
                           long segWidth,
                           long startX,
                           long startY,
                           __global int *rows,
                           __global int *changed) {        
    // minus two then f or g(x) then plus oneto ignore 1px border
    __private long global_idx = (long) get_global_id(0);
    __private long x = (global_idx % segWidth) + startX;
    __private long y = (global_idx / segWidth) + startY;
        
    int this = rows[y * width + x];
    if (this == 0)
        return;
    
    for (int itter = 0; itter < 10; itter++) {   
        int count = 0;
        
        if (rows[y * width + x - 1] != 0)
            count++;
        
        if (rows[y * width + x + 1] != 0)
            count++; 
        
        if (rows[(y - 1) * width + x] != 0)
            count++;
        
        if (rows[(y + 1) * width + x] != 0)
            count++;
        
        if (count < 2) {
            rows[y * width + x] = 0;     
            *changed = 1;            
            break;          
        }
    }
}

__kernel void set_changed_to_zero(__global int *changed) {    
    *changed = 0;
}
