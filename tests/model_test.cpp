#define main greenplan_entry
#include "../src/main.cpp"
#undef main
#include <cassert>
int main() {
    Config c;c.months=2;
    Individual a;a.x.assign(dim(c),2000);repair(a,c);
    for(int f=0;f<factories;++f) for(int m=0;m<c.months;++m) {
        double sum=0;
        for(int p=0;p<products;++p) {double v=a.x[index(f,m,p,c)];assert(v>=0);assert(compatible(f,p)||v==0);sum+=v;}
        assert(sum<=cap(f)+1e-8);
    }
    auto once=a.x;repair(a,c);for(size_t i=0;i<once.size();++i)assert(std::abs(once[i]-a.x[i])<1e-8);
    Individual zero;zero.x.assign(dim(c),0);auto v=evaluate(zero,c);for(double x:v)assert(x==0);
    Individual good,bad;good.f={1,1,1};bad.f={2,2,2};assert(dominates(good,bad));assert(!dominates(bad,good));
    std::vector<Individual> pool{bad,good,good,bad};auto chosen=select(pool,2);assert(chosen.size()==2);for(auto& x:chosen)assert(x.f==good.f);
    a.x.assign(dim(c),0);a.x[index(0,0,1,c)]=100;v=evaluate(a,c);assert(std::abs(v[0]+11000)<1e-8);assert(std::abs(v[2]-150)<1e-8);
    std::cout<<"model invariants passed\n";
}
