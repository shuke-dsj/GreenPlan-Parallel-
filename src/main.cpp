// GreenPlan: deterministic synthetic production planning, NSGA-III-inspired selection.
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _OPENMP
#include <omp.h>
#endif
#ifdef HAVE_MPI
#include <mpi.h>
#endif
#ifdef _MSC_VER
#include <ppl.h>
#endif

using Clock = std::chrono::steady_clock;
using Objectives = std::array<double, 3>;
struct Individual { std::vector<double> x; Objectives f{}; int rank=0; };
struct Config {
    int population=100, generations=50, months=12, threads=4;
    unsigned seed=42;
    std::string backend="serial", output="pareto.csv";
};
constexpr int factories=5, products=8;
constexpr double capacity=1000;
const double price[products]={200,180,250,220,300,280,150,160};
const double cost[products]={80,70,100,90,120,110,60,65};
const double carbon[products]={1.2,1.5,.8,1.,.6,.7,1.8,1.6};
bool compatible(int f,int p) { return (f+p)%4!=0; }
int dim(const Config& c) { return factories*products*c.months; }
int index(int f,int m,int p,const Config& c) { return (f*c.months+m)*products+p; }
double cap(int f) { return capacity*(.8+.1*f); }
// Project each factory/month onto capacity and compatibility constraints.
void repair(Individual& a,const Config& c) {
    for(int f=0;f<factories;++f) for(int m=0;m<c.months;++m) {
        double sum=0;
        for(int p=0;p<products;++p) {
            auto& v=a.x[index(f,m,p,c)];
            v=compatible(f,p)?std::max(0.,v):0.; sum+=v;
        }
        if(sum>cap(f)) for(int p=0;p<products;++p) a.x[index(f,m,p,c)]*=cap(f)/sum;
    }
}
Objectives evaluate(const Individual& a,const Config& c) {
    double profit=0,emissions=0,used=0,totalcap=0;
    for(int m=0;m<c.months;++m) {
        for(int p=0;p<products;++p) {
            double made=0;
            for(int f=0;f<factories;++f) made+=a.x[index(f,m,p,c)];
            // Demand is global per product/month, not counted once per factory.
            double demand=420+40*p+60*std::sin(6.283185307179586*m/12.);
            profit+=price[p]*std::min(made,demand)-cost[p]*made;
            emissions+=carbon[p]*made; used+=made;
        }
        for(int f=0;f<factories;++f) totalcap+=cap(f);
    }
    return {-profit,-used/totalcap,emissions}; // all objectives minimized
}
bool dominates(const Individual& a,const Individual& b) {
    bool strict=false;
    for(int k=0;k<3;++k) { if(a.f[k]>b.f[k]) return false; strict|=a.f[k]<b.f[k]; }
    return strict;
}
std::vector<std::vector<int>> sort_fronts(std::vector<Individual>& pop) {
    [[maybe_unused]] int n=static_cast<int>(pop.size());
    std::vector<int> count(n); std::vector<std::vector<int>> edges(n),fronts(1);
    for(int i=0;i<n;++i) for(int j=i+1;j<n;++j) {
        if(dominates(pop[i],pop[j])) {edges[i].push_back(j); ++count[j];}
        else if(dominates(pop[j],pop[i])) {edges[j].push_back(i); ++count[i];}
    }
    for(int i=0;i<n;++i) if(!count[i]) {fronts[0].push_back(i);pop[i].rank=0;}
    for(size_t f=0;f<fronts.size()&&!fronts[f].empty();++f) {
        std::vector<int> next;
        for(int i:fronts[f]) for(int j:edges[i]) if(--count[j]==0) {
            pop[j].rank=static_cast<int>(f)+1;next.push_back(j);
        }
        if(!next.empty()) fronts.push_back(std::move(next));
    }
    return fronts;
}
std::vector<Individual> select(std::vector<Individual>& pool,int n) {
    auto fronts=sort_fronts(pool);
    Objectives lo=pool[0].f,hi=lo;
    for(auto& a:pool) for(int k=0;k<3;++k) {lo[k]=std::min(lo[k],a.f[k]);hi[k]=std::max(hi[k],a.f[k]);}
    // Das-Dennis simplex reference directions, 6 divisions => 28 directions.
    std::vector<Objectives> refs;
    for(int i=0;i<=6;++i) for(int j=0;j<=6-i;++j) {
        Objectives r{double(i),double(j),double(6-i-j)};
        double norm=std::sqrt(std::inner_product(r.begin(),r.end(),r.begin(),0.));
        for(double& v:r) v/=norm;
        refs.push_back(r);
    }
    std::vector<int> association(pool.size()),occupancy(refs.size());
    std::vector<double> distance(pool.size());
    for(size_t i=0;i<pool.size();++i) {
        Objectives z{};for(int k=0;k<3;++k) z[k]=(pool[i].f[k]-lo[k])/std::max(1e-12,hi[k]-lo[k]);
        double best=1e100;
        for(size_t r=0;r<refs.size();++r) {
            double dot=std::inner_product(z.begin(),z.end(),refs[r].begin(),0.),d=0;
            for(int k=0;k<3;++k) d+=std::pow(z[k]-dot*refs[r][k],2);
            if(d<best) {best=d;association[i]=static_cast<int>(r);}
        }
        distance[i]=best;
    }
    std::vector<Individual> result;result.reserve(n);
    for(auto front:fronts) {
        if(result.size()+front.size()<=static_cast<size_t>(n)) {
            for(int i:front) {result.push_back(pool[i]);++occupancy[association[i]];}
        } else {
            while(result.size()<static_cast<size_t>(n)) {
                auto it=std::min_element(front.begin(),front.end(),[&](int a,int b) {
                    int oa=occupancy[association[a]],ob=occupancy[association[b]];
                    if(oa!=ob) return oa<ob;
                    if(distance[a]!=distance[b]) return distance[a]<distance[b];
                    return a<b;
                });
                int i=*it;result.push_back(pool[i]);++occupancy[association[i]];front.erase(it);
            }
            break;
        }
    }
    return result;
}
void evaluate_population(std::vector<Individual>& pop,const Config& c,int rank,int size) {
    (void)rank;(void)size;
    int n=static_cast<int>(pop.size());
    if(c.backend=="mpi") {
#ifdef HAVE_MPI
        int base=n/size,rem=n%size,local=base+(rank<rem),start=rank*base+std::min(rank,rem),d=dim(c);
        std::vector<double> flat(static_cast<size_t>(n)*d);
        if(rank==0) for(int i=0;i<n;++i) std::copy(pop[i].x.begin(),pop[i].x.end(),flat.begin()+static_cast<size_t>(i)*d);
        MPI_Bcast(flat.data(),n*d,MPI_DOUBLE,0,MPI_COMM_WORLD);
        std::vector<double> values(local*3),all(n*3);std::vector<int> counts(size),displs(size);
        for(int r=0;r<size;++r) {counts[r]=(base+(r<rem))*3;displs[r]=(r*base+std::min(r,rem))*3;}
        for(int i=0;i<local;++i) {
            Individual a;auto begin=flat.begin()+static_cast<size_t>(start+i)*d;a.x.assign(begin,begin+d);
            auto v=evaluate(a,c);std::copy(v.begin(),v.end(),values.begin()+i*3);
        }
        MPI_Gatherv(values.data(),local*3,MPI_DOUBLE,all.data(),counts.data(),displs.data(),MPI_DOUBLE,0,MPI_COMM_WORLD);
        if(rank==0) for(int i=0;i<n;++i) std::copy_n(all.begin()+i*3,3,pop[i].f.begin());
#endif
    } else if(c.backend=="openmp") {
#ifdef _OPENMP
#pragma omp parallel for schedule(static) num_threads(c.threads)
        for(int i=0;i<n;++i) pop[i].f=evaluate(pop[i],c);
#endif
    } else if(c.backend=="ppl") {
#ifdef _MSC_VER
        concurrency::parallel_for(0,n,[&](int i){pop[i].f=evaluate(pop[i],c);});
#endif
    } else for(auto& a:pop) a.f=evaluate(a,c);
}
Config parse(int argc,char** argv) {
    Config c;
    for(int i=1;i<argc;++i) {
        std::string key=argv[i];if(i+1==argc) throw std::runtime_error("missing option value");
        std::string v=argv[++i];
        if(key=="--population") c.population=std::stoi(v);
        else if(key=="--generations") c.generations=std::stoi(v);
        else if(key=="--months") c.months=std::stoi(v);
        else if(key=="--threads") c.threads=std::stoi(v);
        else if(key=="--seed") c.seed=static_cast<unsigned>(std::stoul(v));
        else if(key=="--backend") c.backend=v;
        else if(key=="--output") c.output=v;
        else throw std::runtime_error("unknown option: "+key);
    }
    if(c.population<4||c.population>5000||c.generations<1||c.generations>10000||c.months<1||c.months>120||c.threads<1) throw std::runtime_error("invalid workload size");
    if(c.backend!="serial"&&c.backend!="openmp"&&c.backend!="mpi"&&c.backend!="ppl") throw std::runtime_error("unknown backend");
#ifndef _OPENMP
    if(c.backend=="openmp") throw std::runtime_error("OpenMP not compiled");
#endif
#ifndef HAVE_MPI
    if(c.backend=="mpi") throw std::runtime_error("MPI not compiled");
#endif
#ifndef _MSC_VER
    if(c.backend=="ppl") throw std::runtime_error("PPL requires MSVC");
#endif
    if(static_cast<long long>(c.population)*dim(c)>20000000) throw std::runtime_error("workload exceeds memory guard");
    return c;
}
int main(int argc,char** argv) {
    int rank=0,size=1;
#ifdef HAVE_MPI
    MPI_Init(&argc,&argv);MPI_Comm_rank(MPI_COMM_WORLD,&rank);MPI_Comm_size(MPI_COMM_WORLD,&size);
#endif
    try {
        Config c=parse(argc,argv);
        if(size>1&&c.backend!="mpi") throw std::runtime_error("multi-process launch requires --backend mpi");
        std::mt19937 rng(c.seed);std::uniform_real_distribution<double> u(0,1);
        std::vector<Individual> pop(c.population);
        auto start=Clock::now();
        if(rank==0) for(auto& a:pop) {a.x.resize(dim(c));for(double& v:a.x)v=u(rng)*200;repair(a,c);}
        evaluate_population(pop,c,rank,size);
        if(rank==0) sort_fronts(pop);
        double eval_seconds=0;
        for(int gen=0;gen<c.generations;++gen) {
            std::vector<Individual> children(c.population);
            if(rank==0) {
                auto parent=[&]() -> const Individual& {
                    int a=static_cast<int>(rng()%pop.size()),b=static_cast<int>(rng()%pop.size());return pop[a].rank<=pop[b].rank?pop[a]:pop[b];
                };
                for(auto& child:children) {
                    const auto& a=parent();const auto& b=parent();child.x.resize(dim(c));
                    for(int j=0;j<dim(c);++j) {
                        double mix=u(rng);child.x[j]=mix*a.x[j]+(1-mix)*b.x[j];
                        if(u(rng)<1./dim(c)) child.x[j]+=(u(rng)-.5)*400;
                    }
                    repair(child,c);
                }
            }
            auto t=Clock::now();evaluate_population(children,c,rank,size);
            eval_seconds+=std::chrono::duration<double>(Clock::now()-t).count();
            if(rank==0) {auto pool=pop;pool.insert(pool.end(),children.begin(),children.end());pop=select(pool,c.population);}
        }
        if(rank==0) {
            sort_fronts(pop);std::ofstream out(c.output);if(!out) throw std::runtime_error("cannot open output");
            out<<"profit,utilization,carbon,rank\n"<<std::setprecision(17);
            for(auto& a:pop) if(a.rank==0) out<<-a.f[0]<<','<<-a.f[1]<<','<<a.f[2]<<','<<a.rank<<'\n';
            std::cout<<std::setprecision(9)<<"backend,population,generations,months,threads,processes,seed,total_seconds,evaluation_seconds\n"
                <<c.backend<<','<<c.population<<','<<c.generations<<','<<c.months<<','<<c.threads<<','<<size<<','<<c.seed<<','
                <<std::chrono::duration<double>(Clock::now()-start).count()<<','<<eval_seconds<<'\n';
        }
    } catch(const std::exception& e) {
        std::cerr<<e.what()<<'\n';
#ifdef HAVE_MPI
        MPI_Abort(MPI_COMM_WORLD,1);
#endif
        return 1;
    }
#ifdef HAVE_MPI
    MPI_Finalize();
#endif
    return 0;
}
