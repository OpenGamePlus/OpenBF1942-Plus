import unittest

from con import Con, Vfs


def vm_with(files):
    return Con(Vfs({k: v.encode() for k, v in files.items()}))


class ConTest(unittest.TestCase):
    def test_active_object_model(self):
        vm = vm_with({})
        vm.execute("ObjectTemplate.create Bundle Jeep\nObjectTemplate.addTemplate Wheel\n"
                   "Object.create Jeep\nObject.absolutePosition 1/2/3\n", {})
        self.assertEqual(vm.templates["objecttemplate"]["jeep"]["props"], [("addtemplate", ["Wheel"])])
        self.assertEqual(vm.instances[0]["props"], [("absoluteposition", ["1/2/3"])])
        self.assertFalse(vm.undefined_instances())

    def test_comments_conditions_and_args(self):
        vm = vm_with({"lvl/sub/a.con": "if v_arg1 == host\nObject.create A\nelse\nObject.create B\nendIf\n"})
        vm.execute("rem x\nremObject.create Z\nbeginrem\nObject.create Z\nendrem\nrun sub/a host\nrun sub/a.con client\n",
                   {}, "lvl")
        self.assertEqual([i["template"] for i in vm.instances], ["A", "B"])

    def test_bare_run_keeps_caller_args(self):
        vm = vm_with({"cp.con": "if v_arg1 == host\nObject.create CP\nendIf\n", "mode.con": "run cp\nrun cp client\n"})
        vm.run("mode.con", ["host"])
        self.assertEqual([i["template"] for i in vm.instances], ["CP"])
        self.assertEqual(vm.args, [])

    def test_nested_if_inside_false_branch(self):
        vm = vm_with({})
        vm.execute("if a == b\nif c == c\nObject.create X\nelse\nObject.create Y\nendIf\nendIf\n", {})
        self.assertEqual(vm.instances, [])

    def test_resolution_order_and_missing(self):
        vm = vm_with({"lvl/conquest/cp.con": "Object.create Level", "x/y/cp.con": "Object.create Script",
                      "common/z.inc": "Object.create Root"})
        vm.level_dir = "lvl"
        vm.run("x/y/gametypes.con")  # missing
        vm.execute("run conquest/cp\nrun ../y/cp\ninclude common/z.inc\n", {}, "x/y")
        self.assertEqual([i["template"] for i in vm.instances], ["Level", "Script", "Root"])
        self.assertEqual(vm.missing["x/y/gametypes.con"], 1)

    def test_echo_into_var(self):
        vm = vm_with({})
        vm.execute("var v_name\necho v_arg1 -> v_name\nObject.create v_name\n", {"v_arg1": "Tiger"})
        self.assertEqual(vm.instances[0]["template"], "Tiger")


if __name__ == "__main__":
    unittest.main()
