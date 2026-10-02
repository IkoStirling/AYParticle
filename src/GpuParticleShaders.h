#pragma once
namespace ayt::particle::gpu {
inline constexpr const char* compute=R"(
#include <bgfx_compute.sh>
BUFFER_RW(states, vec4, 0);
BUFFER_WO(vertices, vec4, 1);
uniform vec4 u_gp[20];
uniform vec4 u_gpSize[32];
uniform vec4 u_gpOpacity[32];
uniform vec4 u_gpGradient[64];
uniform vec4 u_gpSource[39];
uniform vec4 u_gpSourceKeys[832];
uniform vec4 u_gpChoices[104];
uniform vec4 u_gpBool[3];
uint mixBits(uint value) { value^=value>>16u;value*=0x7feb352du;value^=value>>15u;value*=0x846ca68bu;return value^(value>>16u); }
float sourceRandom(uint id,float generation,uint key,float scope) {
    uint seed=uint(u_gp[18].x)|(uint(u_gp[18].y)<<16u);
    uint hash=mixBits(seed^mixBits(key+0x9e3779b9u));
    if(scope>0.5) {
        uint low=uint(u_gp[18].z)|(uint(u_gp[18].w)<<16u);
        uint high=uint(u_gp[19].x)|(uint(u_gp[19].y)<<16u);
        hash=mixBits(hash^mixBits(low)^mixBits(high));
    }
    if(scope>1.5) {uint birth=(uint(generation)-1u)*uint(u_gp[0].x)+id+1u;hash=mixBits(hash^mixBits(birth)^mixBits(0u));}
    return float(hash>>8u)*(1.0/16777216.0);
}
float sourceCurve(int base,int count,float time) {
    for(int key=1;key<32;++key) {
        if(key>=count) break;
        vec4 a=u_gpSourceKeys[base+key-1];vec4 b=u_gpSourceKeys[base+key];
        if(time<=b.x)return mix(a.y,b.y,clamp((time-a.x)/(b.x-a.x),0.0,1.0));
    }
    return u_gpSourceKeys[base+count-1].y;
}
float sourceValue(int key,float time,float random,float legacy) {
    vec4 a=u_gpSource[key*3];vec4 b=u_gpSource[key*3+1];int mode=int(a.x);
    if(mode==0)return legacy;
    if(mode==1)return a.z;
    if(mode==2)return mix(a.w,b.x,random);
    if(mode==3){for(int item=0;item<8;++item){if(item>=int(b.w))break;vec4 choice=u_gpChoices[key*8+item];if(random<choice.y)return choice.x;}return u_gpChoices[key*8+int(b.w)-1].x;}
    float low=sourceCurve(key*64,int(b.y),time);
    return mode==4?low:mix(low,sourceCurve(key*64+32,int(b.z),time),random);
}
float birthValue(uint id,float generation,int key,float legacy) {
    float random=sourceRandom(id,generation,uint(key),u_gpSource[key*3].y);
    if(key==5&&int(u_gpSource[key*3].x)==2){float lo=floor(u_gpSource[key*3].w),hi=floor(u_gpSource[key*3+1].x);return min(hi,lo+floor(random*(hi-lo+1.0)));}
    return sourceValue(key,u_gp[16].x,random,legacy);
}
bool sourceBool(uint id,float generation,int key) {
    vec4 source=u_gpBool[key];return source.w<0.5?source.x>0.5:sourceRandom(id,generation,uint(100+key),source.z)<source.y;
}
float sizeTrack(float age) {
    int count=int(u_gp[11].y);
    if(count==0) return mix(1.0,u_gp[11].x,age);
    for(int key=1;key<32;++key) {
        if(key>=count) break;
        if(age<=u_gpSize[key].x) return mix(u_gpSize[key-1].y,u_gpSize[key].y,
            clamp((age-u_gpSize[key-1].x)/(u_gpSize[key].x-u_gpSize[key-1].x),0.0,1.0));
    }
    return u_gpSize[count-1].y;
}
vec4 colorTrack(float age) {
    int count=int(u_gp[11].w);
    if(count==0) return mix(u_gp[9],u_gp[10],age);
    for(int key=1;key<32;++key) {
        if(key>=count) break;
        if(age<=u_gpGradient[key*2].x) return mix(u_gpGradient[(key-1)*2+1],u_gpGradient[key*2+1],
            clamp((age-u_gpGradient[(key-1)*2].x)/(u_gpGradient[key*2].x-u_gpGradient[(key-1)*2].x),0.0,1.0));
    }
    return u_gpGradient[(count-1)*2+1];
}
float opacityTrack(float age,float fallback) {
    int count=int(u_gp[11].z);
    if(count==0) return fallback;
    for(int key=1;key<32;++key) {
        if(key>=count) break;
        if(age<=u_gpOpacity[key].x) return mix(u_gpOpacity[key-1].y,u_gpOpacity[key].y,
            clamp((age-u_gpOpacity[key-1].x)/(u_gpOpacity[key].x-u_gpOpacity[key-1].x),0.0,1.0));
    }
    return u_gpOpacity[count-1].y;
}
vec3 worldVector(vec3 value) { return u_gp[12].xyz*value.x+u_gp[13].xyz*value.y+u_gp[17].xyz*value.z; }
float randomValue(float slot, float generation, float salt) {
    return fract(sin(slot*12.9898+generation*78.233+salt*37.719+u_gp[0].w)*43758.5453);
}
NUM_THREADS(64, 1, 1)
void main() {
    uint id=gl_GlobalInvocationID.x;
    uint capacity=uint(u_gp[0].x);
    if(id>=capacity) return;
    uint base=id*5u;
    vec4 p=vec4(0.0,0.0,0.0,0.0); vec4 v=vec4(0.0,0.0,0.0,0.0); vec4 data=vec4(0.0,0.0,0.0,0.0);
    vec4 animation=vec4(0.0,0.0,0.0,0.0);vec4 motion=vec4(0.0,0.0,0.0,0.0);
    if(u_gp[0].z<0.5) { p=states[base]; v=states[base+1u]; data=states[base+2u];animation=states[base+3u];motion=states[base+4u]; }
    float dt=u_gp[0].y;
    if(v.w>0.0) {
        p.w+=dt;
        v.xyz+=u_gp[4].xyz*dt;
        v.xyz*=exp(-u_gp[4].w*dt);
        p.xyz+=v.xyz*dt;
        data.y+=data.z*dt;
        if(p.w>=v.w) v.w=0.0;
    }
    uint start=uint(u_gp[1].x); uint births=uint(u_gp[1].y);
    uint offset=(id+capacity-start)%capacity;
    if(offset<births) {
        data.w+=1.0;
        float r0=randomValue(float(id),data.w,0.0);
        float r1=randomValue(float(id),data.w,1.0);
        p=vec4(0.0,0.0,0.0,0.0);
        if(u_gp[5].w>0.5&&u_gp[5].w<1.5) p.xy=(vec2(r0,r1)*2.0-vec2(1.0,1.0))*u_gp[6].xy;
        if(u_gp[5].w>1.5&&u_gp[5].w<2.5) p.xy=vec2(cos(r0*6.283185307),sin(r0*6.283185307))*sqrt(r1)*u_gp[6].xy;
        if(u_gp[5].w>2.5&&u_gp[5].w<3.5) p.xyz=(vec3(r0,r1,randomValue(float(id),data.w,6.0))*2.0-vec3(1.0,1.0,1.0))*u_gp[6].xyz;
        if(u_gp[5].w>3.5) {
            float z=randomValue(float(id),data.w,6.0)*2.0-1.0;
            float radius=pow(randomValue(float(id),data.w,7.0),1.0/3.0);
            float xy=sqrt(max(0.0,1.0-z*z))*radius;
            p.xyz=vec3(cos(r0*6.283185307)*xy,sin(r0*6.283185307)*xy,z*radius)*u_gp[6].xyz;
        }
        v=vec4(mix(u_gp[2].xyz,u_gp[3].xyz,vec3(randomValue(float(id),data.w,2.0),
            randomValue(float(id),data.w,3.0),randomValue(float(id),data.w,8.0))), mix(u_gp[7].x,u_gp[7].y,r0));
        if(u_gp[1].w>0.5) { p.xyz=u_gp[5].xyz+worldVector(p.xyz); v.xyz=worldVector(v.xyz); }
        data.x=mix(u_gp[7].z,u_gp[7].w,r1);
        data.y=mix(u_gp[8].x,u_gp[8].y,randomValue(float(id),data.w,4.0));
        data.z=mix(u_gp[8].z,u_gp[8].w,randomValue(float(id),data.w,5.0));
        v.w=birthValue(id,data.w,0,v.w);data.x=birthValue(id,data.w,1,data.x);
        data.y=birthValue(id,data.w,2,data.y);data.z=birthValue(id,data.w,3,data.z);
        animation.x=birthValue(id,data.w,4,u_gp[15].x);animation.y=birthValue(id,data.w,5,0.0);
        animation.z=float((sourceBool(id,data.w,0)?1u:0u)|(sourceBool(id,data.w,1)?2u:0u)|(sourceBool(id,data.w,2)?4u:0u));
        animation.w=sourceRandom(id,data.w,6u,u_gpSource[18].y);
        motion.x=sourceRandom(id,data.w,7u,u_gpSource[21].y);
        motion.y=birthValue(id,data.w,8,0.0);motion.z=birthValue(id,data.w,9,0.0);motion.w=birthValue(id,data.w,10,0.0);
    }
    states[base]=p; states[base+1u]=v; states[base+2u]=data;
    states[base+3u]=animation;states[base+4u]=motion;
    float age=v.w>0.0?clamp(p.w/v.w,0.0,1.0):1.0;
    vec4 color=colorTrack(age); color.w=opacityTrack(age,color.w);
    color.w=sourceValue(7,age,motion.x,color.w);
    float radius=v.w>0.0?0.5*data.x*sourceValue(6,age,animation.w,sizeTrack(age)):0.0;
    if(v.w<=0.0) color=vec4(0.0,0.0,0.0,0.0);
    float c=cos(data.y); float s=sin(data.y);
    float last=u_gp[14].w-1.0;uint flags=uint(animation.z);
    float steps=floor(u_gp[15].z>0.5?age*last:p.w*animation.x);
    float phase=((flags&4u)!=0u?last-floor(animation.y):floor(animation.y))+((flags&4u)!=0u?-steps:steps);
    if(u_gp[15].y>0.5&&u_gp[15].y<1.5)phase=clamp(phase,0.0,last);
    else if(u_gp[15].y>1.5&&last>0.0){phase=mod(phase,last*2.0);if(phase<0.0)phase+=last*2.0;phase=phase<=last?phase:last*2.0-phase;}
    else{phase=mod(phase,u_gp[14].w);if(phase<0.0)phase+=u_gp[14].w;}
    uint frame=uint(u_gp[14].z)+uint(floor(phase));
    float columns=u_gp[14].x; float rows=u_gp[14].y;
    vec2 cell=vec2(float(frame%uint(columns)),float(frame/uint(columns)));
    for(uint corner=0u;corner<4u;++corner) {
        vec2 uv=vec2(corner==1u||corner==2u?1.0:0.0,corner>=2u?1.0:0.0);
        vec2 q=(uv-vec2(0.5,0.5))*2.0*radius;
        vec2 rotated=vec2(c*q.x-s*q.y,s*q.x+c*q.y);
        uint vertex=(id*4u+corner)*3u;
        vec3 center=p.xyz;
        center.x+=motion.y*(sin(6.283185307*motion.z*p.w+motion.w)-sin(motion.w));
        vec3 offset=vec3(rotated,0.0);
        if(u_gp[1].w<0.5) center=u_gp[5].xyz+worldVector(center);
        if(u_gp[17].w<0.5) {
            if(u_gp[1].w<0.5) offset=worldVector(offset);
            vertices[vertex]=vec4(center+offset,1.0);
        } else vertices[vertex]=vec4(center,2.0);
        vec2 atlasUv=(cell+vec2((flags&1u)!=0u?1.0-uv.x:uv.x,(flags&2u)!=0u?uv.y:1.0-uv.y))/vec2(columns,rows);
        vertices[vertex+1u]=vec4(atlasUv,u_gp[17].w>0.5?rotated:vec2(0.0,0.0));
        vertices[vertex+2u]=color;
    }
}
)";
inline constexpr const char* diagnostic=R"(
#include <bgfx_compute.sh>
BUFFER_RO(states, vec4, 0);
BUFFER_RO(vertices, vec4, 2);
IMAGE2D_WO(outputImage, rgba32f, 1);
uniform vec4 u_gp[20];
NUM_THREADS(64, 1, 1)
void main() {
    uint lane=gl_GlobalInvocationID.x;
    uint capacity=uint(u_gp[0].x);
    uint alive=0u;
    for(uint id=lane;id<capacity;id+=64u) if(states[id*5u+1u].w>0.0) ++alive;
    vec4 p=vec4(0.0,0.0,0.0,0.0); vec4 v=vec4(0.0,0.0,0.0,0.0); vec4 data=vec4(0.0,0.0,0.0,0.0);
    if(lane<capacity) { p=states[lane*5u]; v=states[lane*5u+1u]; data=states[lane*5u+2u]; }
    imageStore(outputImage,ivec2(int(lane),0),p);
    imageStore(outputImage,ivec2(int(lane),1),v);
    imageStore(outputImage,ivec2(int(lane),2),data);
    imageStore(outputImage,ivec2(int(lane),3),vec4(float(alive),0.0,0.0,0.0));
    vec4 color=vec4(0.0,0.0,0.0,0.0); vec4 uv=vec4(0.0,0.0,0.0,0.0);
    if(lane<capacity) { color=vertices[lane*12u+2u]; uv=vertices[lane*12u+1u]; }
    imageStore(outputImage,ivec2(int(lane),4),color);
    imageStore(outputImage,ivec2(int(lane),5),uv);
    vec4 vertex=vec4(0.0,0.0,0.0,0.0);
    if(lane<capacity) vertex=vertices[lane*12u];
    imageStore(outputImage,ivec2(int(lane),6),vertex);
    vec4 animation=vec4(0.0,0.0,0.0,0.0),motion=vec4(0.0,0.0,0.0,0.0);
    if(lane<capacity){animation=states[lane*5u+3u];motion=states[lane*5u+4u];}
    imageStore(outputImage,ivec2(int(lane),7),animation);imageStore(outputImage,ivec2(int(lane),8),motion);
}
)";
inline constexpr const char* vertex=R"(
$input a_position, a_texcoord0, a_color0
$output v_uv, v_color
#include <bgfx_shader.sh>
void main() {
    vec3 position=a_position.xyz;
    if(a_position.w>1.5) {
        // Shaderc's D3D11 matrix indexing exposes the camera basis as rows.
        vec3 right=u_view[0].xyz;
        vec3 up=u_view[1].xyz;
        position+=right*a_texcoord0.z+up*a_texcoord0.w;
    }
    gl_Position=mul(u_viewProj,vec4(position,1.0)); v_uv=a_texcoord0.xy; v_color=a_color0;
}
)";
inline constexpr const char* fragment=R"(
$input v_uv, v_color
#include <bgfx_shader.sh>
SAMPLER2D(s_particle,0);
void main() { gl_FragColor=texture2D(s_particle,v_uv)*v_color; }
)";
inline constexpr const char* varyings=R"(
vec4 a_position : POSITION;
vec4 a_texcoord0 : TEXCOORD0;
vec4 a_color0 : COLOR0;
vec2 v_uv : TEXCOORD0;
vec4 v_color : COLOR0;
)";
}
